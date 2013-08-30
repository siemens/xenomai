/*!\file
 * \brief Interrupt management.
 * \author Philippe Gerum
 *
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) 2005,2006 Dmitry Adamushko <dmitry.adamushko@gmail.com>.
 * Copyright (C) 2007 Jan Kiszka <jan.kiszka@web.de>.
 *
 * Xenomai is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * \ingroup intr
 */

/*!
 * \ingroup nucleus
 * \defgroup intr Interrupt management.
 *
 * Interrupt management.
 *
 *@{*/

#include <nucleus/pod.h>
#include <nucleus/intr.h>
#include <nucleus/stat.h>
#include <asm/xenomai/bits/intr.h>

#define XNINTR_MAX_UNHANDLED	1000

DEFINE_PRIVATE_XNLOCK(intrlock);

#ifdef CONFIG_XENO_OPT_STATS
xnintr_t nkclock;	     /* Only for statistics */
static int xnintr_count = 1; /* Number of attached xnintr objects + nkclock */
static int xnintr_list_rev;  /* Modification counter of xnintr list */

/* Both functions update xnintr_list_rev at the very end.
 * This guarantees that module.c::stat_seq_open() won't get
 * an up-to-date xnintr_list_rev and old xnintr_count. */

static inline void xnintr_stat_counter_inc(void)
{
	xnintr_count++;
	xnarch_memory_barrier();
	xnintr_list_rev++;
}

static inline void xnintr_stat_counter_dec(void)
{
	xnintr_count--;
	xnarch_memory_barrier();
	xnintr_list_rev++;
}

static inline void xnintr_sync_stat_references(xnintr_t *intr)
{
	int cpu;

	for_each_online_cpu(cpu) {
		struct xnsched *sched = xnpod_sched_slot(cpu);
		/* Synchronize on all dangling references to go away. */
		while (sched->current_account == &intr->stat[cpu].account)
			cpu_relax();
	}
}
#else
static inline void xnintr_stat_counter_inc(void) {}
static inline void xnintr_stat_counter_dec(void) {}
static inline void xnintr_sync_stat_references(xnintr_t *intr) {}
#endif /* CONFIG_XENO_OPT_STATS */

static void xnintr_irq_handler(unsigned irq, void *cookie);

void xnintr_host_tick(struct xnsched *sched) /* Interrupts off. */
{
	__clrbits(sched->lflags, XNHTICK);
	xnarch_relay_tick();
}

/* Low-level clock irq handler. */

void xnintr_clock_handler(void)
{
	struct xnsched *sched = xnpod_current_sched();
	unsigned int cpu = xnsched_cpu(sched);
	xnstat_exectime_t *prev;

	if (!cpu_isset(cpu, xnarch_supported_cpus)) {
		xnarch_relay_tick();
		return;
	}

	prev = xnstat_exectime_switch(sched, &nkclock.stat[cpu].account);
	xnstat_counter_inc(&nkclock.stat[cpu].hits);

	trace_mark(xn_nucleus, irq_enter, "irq %u", XNARCH_TIMER_IRQ);
	trace_mark(xn_nucleus, tbase_tick, "base %s", nktbase.name);

	++sched->inesting;
	__setbits(sched->lflags, XNINIRQ);

	xnlock_get(&nklock);
	xntimer_tick_aperiodic();
	xnlock_put(&nklock);

	xnstat_exectime_switch(sched, prev);

	if (--sched->inesting == 0) {
		__clrbits(sched->lflags, XNINIRQ);
		xnpod_schedule();
		sched = xnpod_current_sched();
	}
	/*
	 * If the clock interrupt preempted a real-time thread, any
	 * transition to the root thread has already triggered a host
	 * tick propagation from xnpod_schedule(), so at this point,
	 * we only need to propagate the host tick in case the
	 * interrupt preempted the root thread.
	 */
	if (testbits(sched->lflags, XNHTICK) &&
	    xnthread_test_state(sched->curr, XNROOT))
		xnintr_host_tick(sched);

	trace_mark(xn_nucleus, irq_exit, "irq %u", XNARCH_TIMER_IRQ);
}

/* Optional support for shared interrupts. */

#ifdef CONFIG_XENO_OPT_SHIRQ

typedef struct xnintr_irq {

	DECLARE_XNLOCK(lock);

	xnintr_t *handlers;
	int unhandled;

} ____cacheline_aligned_in_smp xnintr_irq_t;

static xnintr_irq_t xnirqs[XNARCH_NR_IRQS];

static inline xnintr_t *xnintr_shirq_first(unsigned irq)
{
	return xnirqs[irq].handlers;
}

static inline xnintr_t *xnintr_shirq_next(xnintr_t *prev)
{
	return prev->next;
}

/*
 * Low-level interrupt handler dispatching the user-defined ISRs for
 * shared interrupts -- Called with interrupts off.
 */
static void xnintr_shirq_handler(unsigned irq, void *cookie)
{
	struct xnsched *sched = xnpod_current_sched();
	xnintr_irq_t *shirq = &xnirqs[irq];
	xnstat_exectime_t *prev;
	xnticks_t start;
	xnintr_t *intr;
	int s = 0, ret;

	prev  = xnstat_exectime_get_current(sched);
	start = xnstat_exectime_now();
	trace_mark(xn_nucleus, irq_enter, "irq %u", irq);

	++sched->inesting;
	__setbits(sched->lflags, XNINIRQ);

	xnlock_get(&shirq->lock);
	intr = shirq->handlers;

	while (intr) {
		/*
		 * NOTE: We assume that no CPU migration will occur
		 * while running the interrupt service routine.
		 */
		ret = intr->isr(intr);
		s |= ret;

		if (ret & XN_ISR_HANDLED) {
			xnstat_counter_inc(
				&intr->stat[xnsched_cpu(sched)].hits);
			xnstat_exectime_lazy_switch(sched,
				&intr->stat[xnsched_cpu(sched)].account,
				start);
			start = xnstat_exectime_now();
		}

		intr = intr->next;
	}

	xnlock_put(&shirq->lock);

	if (unlikely(s == XN_ISR_NONE)) {
		if (++shirq->unhandled == XNINTR_MAX_UNHANDLED) {
			xnlogerr("%s: IRQ%d not handled. Disabling IRQ "
				 "line.\n", __FUNCTION__, irq);
			s |= XN_ISR_NOENABLE;
		}
	} else
		shirq->unhandled = 0;

	if (s & XN_ISR_PROPAGATE)
		xnarch_chain_irq(irq);
	else if (!(s & XN_ISR_NOENABLE))
		xnarch_end_irq(irq);

	xnstat_exectime_switch(sched, prev);

	if (--sched->inesting == 0) {
		__clrbits(sched->lflags, XNINIRQ);
		xnpod_schedule();
	}

	trace_mark(xn_nucleus, irq_exit, "irq %u", irq);
}

/*
 * Low-level interrupt handler dispatching the user-defined ISRs for
 * shared edge-triggered interrupts -- Called with interrupts off.
 */
static void xnintr_edge_shirq_handler(unsigned irq, void *cookie)
{
	const int MAX_EDGEIRQ_COUNTER = 128;
	struct xnsched *sched = xnpod_current_sched();
	xnintr_irq_t *shirq = &xnirqs[irq];
	int s = 0, counter = 0, ret, code;
	struct xnintr *intr, *end = NULL;
	xnstat_exectime_t *prev;
	xnticks_t start;

	prev  = xnstat_exectime_get_current(sched);
	start = xnstat_exectime_now();
	trace_mark(xn_nucleus, irq_enter, "irq %u", irq);

	++sched->inesting;
	__setbits(sched->lflags, XNINIRQ);

	xnlock_get(&shirq->lock);
	intr = shirq->handlers;

	while (intr != end) {
		xnstat_exectime_switch(sched,
			&intr->stat[xnsched_cpu(sched)].account);
		/*
		 * NOTE: We assume that no CPU migration will occur
		 * while running the interrupt service routine.
		 */
		ret = intr->isr(intr);
		code = ret & ~XN_ISR_BITMASK;
		s |= ret;

		if (code == XN_ISR_HANDLED) {
			end = NULL;
			xnstat_counter_inc(
				&intr->stat[xnsched_cpu(sched)].hits);
			xnstat_exectime_lazy_switch(sched,
				&intr->stat[xnsched_cpu(sched)].account,
				start);
			start = xnstat_exectime_now();
		} else if (end == NULL)
			end = intr;

		if (counter++ > MAX_EDGEIRQ_COUNTER)
			break;

		if (!(intr = intr->next))
			intr = shirq->handlers;
	}

	xnlock_put(&shirq->lock);

	if (counter > MAX_EDGEIRQ_COUNTER)
		xnlogerr
		    ("xnintr_edge_shirq_handler() : failed to get the IRQ%d line free.\n",
		     irq);

	if (unlikely(s == XN_ISR_NONE)) {
		if (++shirq->unhandled == XNINTR_MAX_UNHANDLED) {
			xnlogerr("%s: IRQ%d not handled. Disabling IRQ "
				 "line.\n", __FUNCTION__, irq);
			s |= XN_ISR_NOENABLE;
		}
	} else
		shirq->unhandled = 0;

	if (s & XN_ISR_PROPAGATE)
		xnarch_chain_irq(irq);
	else if (!(s & XN_ISR_NOENABLE))
		xnarch_end_irq(irq);

	xnstat_exectime_switch(sched, prev);

	if (--sched->inesting == 0) {
		__clrbits(sched->lflags, XNINIRQ);
		xnpod_schedule();
	}

	trace_mark(xn_nucleus, irq_exit, "irq %u", irq);
}

static inline int xnintr_irq_attach(xnintr_t *intr)
{
	xnintr_irq_t *shirq = &xnirqs[intr->irq];
	xnintr_t *prev, **p = &shirq->handlers;
	int err;

	if ((prev = *p) != NULL) {
		/* Check on whether the shared mode is allowed. */
		if (!(prev->flags & intr->flags & XN_ISR_SHARED) ||
		    (prev->iack != intr->iack)
		    || ((prev->flags & XN_ISR_EDGE) !=
			(intr->flags & XN_ISR_EDGE)))
			return -EBUSY;

		/* Get a position at the end of the list to insert the new element. */
		while (prev) {
			p = &prev->next;
			prev = *p;
		}
	} else {
		/* Initialize the corresponding interrupt channel */
		void (*handler) (unsigned, void *) = &xnintr_irq_handler;

		if (intr->flags & XN_ISR_SHARED) {
			if (intr->flags & XN_ISR_EDGE)
				handler = &xnintr_edge_shirq_handler;
			else
				handler = &xnintr_shirq_handler;

		}
		shirq->unhandled = 0;

		err = xnarch_hook_irq(intr->irq, handler,
				      (rthal_irq_ackfn_t)intr->iack, intr);
		if (err)
			return err;
	}

	intr->next = NULL;

	/* Add the given interrupt object. No need to synchronise with the IRQ
	   handler, we are only extending the chain. */
	*p = intr;

	return 0;
}

static inline int xnintr_irq_detach(xnintr_t *intr)
{
	xnintr_irq_t *shirq = &xnirqs[intr->irq];
	xnintr_t *e, **p = &shirq->handlers;
	int err = 0;

	while ((e = *p) != NULL) {
		if (e == intr) {
			/* Remove the given interrupt object from the list. */
			xnlock_get(&shirq->lock);
			*p = e->next;
			xnlock_put(&shirq->lock);

			xnintr_sync_stat_references(intr);

			/* Release the IRQ line if this was the last user */
			if (shirq->handlers == NULL)
				err = xnarch_release_irq(intr->irq);

			return err;
		}
		p = &e->next;
	}

	xnlogerr("attempted to detach a non previously attached interrupt "
		 "object.\n");
	return err;
}

#else /* !CONFIG_XENO_OPT_SHIRQ */

#if defined(CONFIG_SMP) || XENO_DEBUG(XNLOCK)
typedef struct xnintr_irq {

	DECLARE_XNLOCK(lock);

} ____cacheline_aligned_in_smp xnintr_irq_t;

static xnintr_irq_t xnirqs[XNARCH_NR_IRQS];
#endif /* CONFIG_SMP || XENO_DEBUG(XNLOCK) */

static inline xnintr_t *xnintr_shirq_first(unsigned irq)
{
	return xnarch_get_irq_cookie(irq);
}

static inline xnintr_t *xnintr_shirq_next(xnintr_t *prev)
{
	return NULL;
}

static inline int xnintr_irq_attach(xnintr_t *intr)
{
	return xnarch_hook_irq(intr->irq, &xnintr_irq_handler,
			       (rthal_irq_ackfn_t)intr->iack, intr);
}

static inline int xnintr_irq_detach(xnintr_t *intr)
{
	int irq = intr->irq, ret;

	xnlock_get(&xnirqs[irq].lock);
	ret = xnarch_release_irq(irq);
	xnlock_put(&xnirqs[irq].lock);

	xnintr_sync_stat_references(intr);

	return ret;
}

#endif /* !CONFIG_XENO_OPT_SHIRQ */

/*
 * Low-level interrupt handler dispatching non-shared ISRs -- Called with
 * interrupts off.
 */
static void xnintr_irq_handler(unsigned irq, void *cookie)
{
	struct xnsched *sched = xnpod_current_sched();
	xnstat_exectime_t *prev;
	struct xnintr *intr;
	xnticks_t start;
	int s;

	prev  = xnstat_exectime_get_current(sched);
	start = xnstat_exectime_now();
	trace_mark(xn_nucleus, irq_enter, "irq %u", irq);

	++sched->inesting;
	__setbits(sched->lflags, XNINIRQ);

	xnlock_get(&xnirqs[irq].lock);

#ifdef CONFIG_SMP
	/*
	 * In SMP case, we have to reload the cookie under the per-IRQ
	 * lock to avoid racing with xnintr_detach.  However, we
	 * assume that no CPU migration will occur while running the
	 * interrupt service routine, so the scheduler pointer will
	 * remain valid throughout this function.
	 */
	intr = xnarch_get_irq_cookie(irq);
	if (unlikely(!intr)) {
		s = 0;
		goto unlock_and_exit;
	}
#else
	/* cookie always valid, attach/detach happens with IRQs disabled */
	intr = cookie;
#endif
	s = intr->isr(intr);
	if (unlikely(s == XN_ISR_NONE)) {
		if (++intr->unhandled == XNINTR_MAX_UNHANDLED) {
			xnlogerr("%s: IRQ%d not handled. Disabling IRQ "
				 "line.\n", __FUNCTION__, irq);
			s |= XN_ISR_NOENABLE;
		}
	} else {
		xnstat_counter_inc(&intr->stat[xnsched_cpu(sched)].hits);
		xnstat_exectime_lazy_switch(sched,
			&intr->stat[xnsched_cpu(sched)].account,
			start);
		intr->unhandled = 0;
	}

#ifdef CONFIG_SMP
 unlock_and_exit:
#endif
	xnlock_put(&xnirqs[irq].lock);

	if (s & XN_ISR_PROPAGATE)
		xnarch_chain_irq(irq);
	else if (!(s & XN_ISR_NOENABLE))
		xnarch_end_irq(irq);

	xnstat_exectime_switch(sched, prev);

	if (--sched->inesting == 0) {
		__clrbits(sched->lflags, XNINIRQ);
		xnpod_schedule();
	}

	trace_mark(xn_nucleus, irq_exit, "irq %u", irq);
}

int __init xnintr_mount(void)
{
	int i;
	for (i = 0; i < XNARCH_NR_IRQS; ++i)
		xnlock_init(&xnirqs[i].lock);
	return 0;
}

/*!
 * \fn int xnintr_init (xnintr_t *intr,const char *name,unsigned irq,xnisr_t isr,xniack_t iack,xnflags_t flags)
 * \brief Initialize an interrupt object.
 *
 * Associates an interrupt object with an IRQ line.
 *
 * When an interrupt occurs on the given @a irq line, the ISR is fired
 * in order to deal with the hardware event. The interrupt service
 * code may call any non-suspensive service from the nucleus.
 *
 * Upon receipt of an IRQ, the ISR is immediately called on behalf of
 * the interrupted stack context, the rescheduling procedure is
 * locked, and the interrupt source is masked at hardware level. The
 * status value returned by the ISR is then checked for the following
 * values:
 *
 * - XN_ISR_HANDLED indicates that the interrupt request has been fulfilled
 * by the ISR.
 *
 * - XN_ISR_NONE indicates the opposite to XN_ISR_HANDLED. The ISR must always
 * return this value when it determines that the interrupt request has not been
 * issued by the dedicated hardware device.
 *
 * In addition, one of the following bits may be set by the ISR :
 *
 * NOTE: use these bits with care and only when you do understand their effect
 * on the system.
 * The ISR is not encouraged to use these bits in case it shares the IRQ line
 * with other ISRs in the real-time domain.
 *
 * - XN_ISR_NOENABLE causes the nucleus to ask the real-time control
 * layer _not_ to re-enable the IRQ line (read the following section).
 * xnarch_end_irq() must be called to re-enable the IRQ line later.
 *
 * - XN_ISR_PROPAGATE tells the nucleus to require the real-time
 * control layer to forward the IRQ. For instance, this would cause
 * the Adeos control layer to propagate the interrupt down the
 * interrupt pipeline to other Adeos domains, such as Linux. This is
 * the regular way to share interrupts between the nucleus and the
 * host system. In effect, XN_ISR_PROPAGATE implies XN_ISR_NOENABLE
 * since it would make no sense to re-enable the interrupt channel
 * before the next domain down the pipeline has had a chance to
 * process the propagated interrupt.
 *
 * The nucleus re-enables the IRQ line by default. Over some real-time
 * control layers which mask and acknowledge IRQs, this operation is
 * necessary to revalidate the interrupt channel so that more interrupts
 * can be notified.
 *
 * A count of interrupt receipts is tracked into the interrupt
 * descriptor, and reset to zero each time the interrupt object is
 * attached. Since this count could wrap around, it should be used as
 * an indication of interrupt activity only.
 *
 * @param intr The address of a interrupt object descriptor the
 * nucleus will use to store the object-specific data.  This
 * descriptor must always be valid while the object is active
 * therefore it must be allocated in permanent memory.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * interrupt object or NULL ("<unknown>" will be applied then).
 *
 * @param irq The hardware interrupt channel associated with the
 * interrupt object. This value is architecture-dependent. An
 * interrupt object must then be attached to the hardware interrupt
 * vector using the xnintr_attach() service for the associated IRQs
 * to be directed to this object.
 *
 * @param isr The address of a valid low-level interrupt service
 * routine if this parameter is non-zero. This handler will be called
 * each time the corresponding IRQ is delivered on behalf of an
 * interrupt context.  When called, the ISR is passed the descriptor
 * address of the interrupt object.
 *
 * @param iack The address of an optional interrupt acknowledge
 * routine, aimed at replacing the default one. Only very specific
 * situations actually require to override the default setting for
 * this parameter, like having to acknowledge non-standard PIC
 * hardware. @a iack should return a non-zero value to indicate that
 * the interrupt has been properly acknowledged. If @a iack is NULL,
 * the default routine will be used instead.
 *
 * @param flags A set of creation flags affecting the operation. The
 * valid flags are:
 *
 * - XN_ISR_SHARED enables IRQ-sharing with other interrupt objects.
 *
 * - XN_ISR_EDGE is an additional flag need to be set together with XN_ISR_SHARED
 * to enable IRQ-sharing of edge-triggered interrupts.
 *
 * @return 0 is returned on success. Otherwise, -EINVAL is returned if
 * @a irq is not a valid interrupt number.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 *
 * Rescheduling: never.
 */

int xnintr_init(xnintr_t *intr,
		const char *name,
		unsigned irq, xnisr_t isr, xniack_t iack, xnflags_t flags)
{
	if (irq >= XNARCH_NR_IRQS)
		return -EINVAL;

	intr->irq = irq;
	intr->isr = isr;
	intr->iack = iack;
	intr->cookie = NULL;
	intr->name = name ? : "<unknown>";
	intr->flags = flags;
	intr->unhandled = 0;
	memset(&intr->stat, 0, sizeof(intr->stat));
#ifdef CONFIG_XENO_OPT_SHIRQ
	intr->next = NULL;
#endif

	return 0;
}
EXPORT_SYMBOL_GPL(xnintr_init);

/*!
 * \fn int xnintr_destroy (xnintr_t *intr)
 * \brief Destroy an interrupt object.
 *
 * Destroys an interrupt object previously initialized by
 * xnintr_init(). The interrupt object is automatically detached by a
 * call to xnintr_detach(). No more IRQs will be dispatched by this
 * object after this service has returned.
 *
 * @param intr The descriptor address of the interrupt object to
 * destroy.
 *
 * @return 0 is returned on success. Otherwise, -EINVAL is returned if
 * an error occurred while detaching the interrupt (see
 * xnintr_detach()).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 *
 * Rescheduling: never.
 */

int xnintr_destroy(xnintr_t *intr)
{
	return xnintr_detach(intr);
}
EXPORT_SYMBOL_GPL(xnintr_destroy);

/*!
 * \fn int xnintr_attach (xnintr_t *intr, void *cookie);
 * \brief Attach an interrupt object.
 *
 * Attach an interrupt object previously initialized by
 * xnintr_init(). After this operation is completed, all IRQs received
 * from the corresponding interrupt channel are directed to the
 * object's ISR.
 *
 * @param intr The descriptor address of the interrupt object to
 * attach.
 *
 * @param cookie A user-defined opaque value which is stored into the
 * interrupt object descriptor for further retrieval by the ISR/ISR
 * handlers.
 *
 * @return 0 is returned on success. Otherwise:
 *
 * - -EINVAL is returned if a low-level error occurred while attaching
 * the interrupt.
 *
 * - -EBUSY is returned if the interrupt object was already attached.
 *
 * @note The caller <b>must not</b> hold nklock when invoking this service,
 * this would cause deadlocks.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 *
 * Rescheduling: never.
 *
 * @note Attaching an interrupt resets the tracked number of receipts
 * to zero.
 */

int xnintr_attach(xnintr_t *intr, void *cookie)
{
	int ret;
	spl_t s;

	trace_mark(xn_nucleus, irq_attach, "irq %u name %s",
		   intr->irq, intr->name);

	intr->cookie = cookie;
	memset(&intr->stat, 0, sizeof(intr->stat));

#ifdef CONFIG_SMP
	xnarch_set_irq_affinity(intr->irq, nkaffinity);
#endif /* CONFIG_SMP */

	xnlock_get_irqsave(&intrlock, s);

	if (__testbits(intr->flags, XN_ISR_ATTACHED)) {
		ret = -EBUSY;
		goto out;
	}

	ret = xnintr_irq_attach(intr);
	if (ret)
		goto out;

	__setbits(intr->flags, XN_ISR_ATTACHED);
	xnintr_stat_counter_inc();
out:
	xnlock_put_irqrestore(&intrlock, s);

	return ret;
}
EXPORT_SYMBOL_GPL(xnintr_attach);

/*!
 * \fn int xnintr_detach (xnintr_t *intr)
 * \brief Detach an interrupt object.
 *
 * Detach an interrupt object previously attached by
 * xnintr_attach(). After this operation is completed, no more IRQs
 * are directed to the object's ISR, but the interrupt object itself
 * remains valid. A detached interrupt object can be attached again by
 * a subsequent call to xnintr_attach().
 *
 * @param intr The descriptor address of the interrupt object to
 * detach.
 *
 * @return 0 is returned on success. Otherwise:
 *
 * - -EINVAL is returned if a low-level error occurred while detaching
 * the interrupt, or if the interrupt object was not attached. In both
 * cases, no action is performed.
 *
 * @note The caller <b>must not</b> hold nklock when invoking this service,
 * this would cause deadlocks.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 *
 * Rescheduling: never.
 */
int xnintr_detach(xnintr_t *intr)
{
	int ret;
	spl_t s;

	trace_mark(xn_nucleus, irq_detach, "irq %u", intr->irq);

	xnlock_get_irqsave(&intrlock, s);

	if (!__testbits(intr->flags, XN_ISR_ATTACHED)) {
		ret = -EINVAL;
		goto out;
	}

	__clrbits(intr->flags, XN_ISR_ATTACHED);

	ret = xnintr_irq_detach(intr);
	if (ret)
		goto out;

	xnintr_stat_counter_dec();
 out:
	xnlock_put_irqrestore(&intrlock, s);

	return ret;
}
EXPORT_SYMBOL_GPL(xnintr_detach);

/*!
 * \fn int xnintr_enable (xnintr_t *intr)
 * \brief Enable an interrupt object.
 *
 * Enables the hardware interrupt line associated with an interrupt
 * object. Over real-time control layers which mask and acknowledge
 * IRQs, this operation is necessary to revalidate the interrupt
 * channel so that more interrupts can be notified.

 * @param intr The descriptor address of the interrupt object to
 * enable.
 *
 * @return 0 is returned on success. Otherwise, -EINVAL is returned if
 * a low-level error occurred while enabling the interrupt.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 *
 * Rescheduling: never.
 */

int xnintr_enable(xnintr_t *intr)
{
	trace_mark(xn_nucleus, irq_enable, "irq %u", intr->irq);

	return xnarch_enable_irq(intr->irq);
}
EXPORT_SYMBOL_GPL(xnintr_enable);

/*!
 * \fn int xnintr_disable (xnintr_t *intr)
 * \brief Disable an interrupt object.
 *
 * Disables the hardware interrupt line associated with an interrupt
 * object. This operation invalidates further interrupt requests from
 * the given source until the IRQ line is re-enabled anew.
 *
 * @param intr The descriptor address of the interrupt object to
 * disable.
 *
 * @return 0 is returned on success. Otherwise, -EINVAL is returned if
 * a low-level error occurred while disabling the interrupt.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 *
 * Rescheduling: never.
 */

int xnintr_disable(xnintr_t *intr)
{
	trace_mark(xn_nucleus, irq_disable, "irq %u", intr->irq);

	return xnarch_disable_irq(intr->irq);
}
EXPORT_SYMBOL_GPL(xnintr_disable);

/*!
 * \fn xnarch_cpumask_t xnintr_affinity (xnintr_t *intr, xnarch_cpumask_t cpumask)
 * \brief Set interrupt's processor affinity.
 *
 * Causes the IRQ associated with the interrupt object @a intr to be
 * received only on processors which bits are set in @a cpumask.
 *
 * @param intr The descriptor address of the interrupt object which
 * affinity is to be changed.
 *
 * @param cpumask The new processor affinity of the interrupt object.
 *
 * @return the previous cpumask on success, or an empty mask on
 * failure.
 *
 * @note Depending on architectures, setting more than one bit in @a
 * cpumask could be meaningless.
 */

void xnintr_affinity(xnintr_t *intr, xnarch_cpumask_t cpumask)
{
	trace_mark(xn_nucleus, irq_affinity, "irq %u %lu",
		   intr->irq, *(unsigned long *)&cpumask);

	xnarch_set_irq_affinity(intr->irq, cpumask);
}
EXPORT_SYMBOL_GPL(xnintr_affinity);

#ifdef CONFIG_XENO_OPT_VFILE

#include <nucleus/vfile.h>

static int xnintr_is_timer_irq(int irq)
{
	int cpu;

	for_each_online_cpu(cpu)
		if (irq == XNARCH_PERCPU_TIMER_IRQ(cpu))
			return 1;
	return 0;
}

#ifdef CONFIG_XENO_OPT_STATS
int xnintr_query_init(xnintr_iterator_t *iterator)
{
	iterator->cpu = -1;
	iterator->prev = NULL;

	/* The order is important here: first xnintr_list_rev then
	 * xnintr_count.  On the other hand, xnintr_attach/detach()
	 * update xnintr_count first and then xnintr_list_rev.  This
	 * should guarantee that we can't get an up-to-date
	 * xnintr_list_rev and old xnintr_count here. The other way
	 * around is not a problem as xnintr_query() will notice this
	 * fact later.  Should xnintr_list_rev change later,
	 * xnintr_query() will trigger an appropriate error below. */

	iterator->list_rev = xnintr_list_rev;
	xnarch_memory_barrier();

	return xnintr_count;
}

int xnintr_query_next(int irq, xnintr_iterator_t *iterator, char *name_buf)
{
	xnintr_t *intr;
	xnticks_t last_switch;
	int cpu_no = iterator->cpu + 1;
	int err = 0;
	spl_t s;

	if (cpu_no == xnarch_num_online_cpus())
		cpu_no = 0;
	iterator->cpu = cpu_no;

	xnlock_get_irqsave(&intrlock, s);

	if (iterator->list_rev != xnintr_list_rev) {
		err = -EAGAIN;
		goto unlock_and_exit;
	}

	if (!iterator->prev) {
		if (xnintr_is_timer_irq(irq))
			intr = &nkclock;
		else
			intr = xnintr_shirq_first(irq);
	} else
		intr = xnintr_shirq_next(iterator->prev);

	if (!intr) {
		cpu_no = -1;
		iterator->prev = NULL;
		err = -ENODEV;
		goto unlock_and_exit;
	}

	snprintf(name_buf, XNOBJECT_NAME_LEN, "IRQ%d: %s", irq, intr->name);

	iterator->hits = xnstat_counter_get(&intr->stat[cpu_no].hits);

	last_switch = xnpod_sched_slot(cpu_no)->last_account_switch;

	iterator->exectime_period = intr->stat[cpu_no].account.total;
	iterator->account_period =
		last_switch - intr->stat[cpu_no].account.start;
	intr->stat[cpu_no].sum.total += iterator->exectime_period;
	iterator->exectime_total = intr->stat[cpu_no].sum.total;

	intr->stat[cpu_no].account.total = 0;
	intr->stat[cpu_no].account.start = last_switch;

	/* Proceed to next entry in shared IRQ chain when all CPUs
	 * have been visited for this one. */
	if (cpu_no + 1 == xnarch_num_online_cpus())
		iterator->prev = intr;

     unlock_and_exit:
	xnlock_put_irqrestore(&intrlock, s);

	return err;
}
#endif /* CONFIG_XENO_OPT_STATS */

static inline int format_irq_proc(unsigned int irq,
				  struct xnvfile_regular_iterator *it)
{
	struct xnintr *intr;
	spl_t s;

	if (xnintr_is_timer_irq(irq)) {
		xnvfile_puts(it, "         [timer]");
		return 0;
	}

#ifdef CONFIG_SMP
	if (irq == RTHAL_TIMER_IPI) {
		xnvfile_puts(it, "         [timer-ipi]");
		return 0;
	}
	if (irq == RTHAL_RESCHEDULE_IPI) {
		xnvfile_puts(it, "         [reschedule]");
		return 0;
	}
	if (irq == RTHAL_CRITICAL_IPI) {
		xnvfile_puts(it, "         [sync]");
		return 0;
	}
#endif /* CONFIG_SMP */
	if (rthal_virtual_irq_p(irq)) {
		xnvfile_puts(it, "         [virtual]");
		return 0;
	}

	xnlock_get_irqsave(&intrlock, s);

	intr = xnintr_shirq_first(irq);
	if (intr) {
		xnvfile_puts(it, "        ");

		do {
			xnvfile_putc(it, ' ');
			xnvfile_puts(it, intr->name);
			intr = xnintr_shirq_next(intr);
		} while (intr);
	}

	xnlock_put_irqrestore(&intrlock, s);

	return 0;
}

static int irq_vfile_show(struct xnvfile_regular_iterator *it,
			  void *data)
{
	int cpu, irq;

	/* FIXME: We assume the entire output fits in a single page. */

	xnvfile_puts(it, "IRQ ");

	for_each_online_cpu(cpu)
		xnvfile_printf(it, "        CPU%d", cpu);

	for (irq = 0; irq < XNARCH_NR_IRQS; irq++) {
		if (rthal_irq_handler(&rthal_domain, irq) == NULL)
			continue;

		xnvfile_printf(it, "\n%3d:", irq);

		for_each_online_cpu(cpu) {
			xnvfile_printf(it, "%12lu",
				       rthal_cpudata_irq_hits(&rthal_domain, cpu,
							      irq));
		}

		format_irq_proc(irq, it);
	}

	xnvfile_putc(it, '\n');

	return 0;
}

static struct xnvfile_regular_ops irq_vfile_ops = {
	.show = irq_vfile_show,
};

static struct xnvfile_regular irq_vfile = {
	.ops = &irq_vfile_ops,
};

#ifdef CONFIG_SMP

static int affinity_vfile_show(struct xnvfile_regular_iterator *it,
			       void *data)
{
	unsigned long val = 0;
	int cpu;

	for (cpu = 0; cpu < BITS_PER_LONG; cpu++)
		if (xnarch_cpu_isset(cpu, nkaffinity))
			val |= (1ul << cpu);

	xnvfile_printf(it, "%08lx\n", val);

	return 0;
}

static ssize_t affinity_vfile_store(struct xnvfile_input *input)
{
	xnarch_cpumask_t new_affinity;
	ssize_t ret;
	long val;
	int cpu;

	ret = xnvfile_get_integer(input, &val);
	if (ret < 0)
		return ret;

	xnarch_cpus_clear(new_affinity);

	for (cpu = 0; cpu < BITS_PER_LONG; cpu++, val >>= 1)
		if (val & 1)
			xnarch_cpu_set(cpu, new_affinity);

	xnarch_cpus_and(nkaffinity, new_affinity, xnarch_supported_cpus);

	return ret;
}

static struct xnvfile_regular_ops affinity_vfile_ops = {
	.show = affinity_vfile_show,
	.store = affinity_vfile_store,
};

static struct xnvfile_regular affinity_vfile = {
	.ops = &affinity_vfile_ops,
};

#endif /* CONFIG_SMP */

void xnintr_init_proc(void)
{
	xnvfile_init_regular("irq", &irq_vfile, &nkvfroot);
#ifdef CONFIG_SMP
	xnvfile_init_regular("affinity", &affinity_vfile, &nkvfroot);
#endif /* CONFIG_SMP */
}

void xnintr_cleanup_proc(void)
{
#ifdef CONFIG_SMP
	xnvfile_destroy_regular(&affinity_vfile);
#endif /* CONFIG_SMP */
	xnvfile_destroy_regular(&irq_vfile);
}

#endif /* CONFIG_XENO_OPT_VFILE */

/*@}*/

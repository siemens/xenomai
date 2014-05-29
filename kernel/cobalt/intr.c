/*
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
 * @ingroup nucleus
 * @defgroup intr Interrupt management.
 * @{
*/

#include <linux/mutex.h>
#include <cobalt/kernel/sched.h>
#include <cobalt/kernel/intr.h>
#include <cobalt/kernel/stat.h>
#include <cobalt/kernel/clock.h>
#include <cobalt/kernel/assert.h>
#include <trace/events/cobalt-core.h>

#define XNINTR_MAX_UNHANDLED	1000

static DEFINE_MUTEX(intrlock);

#ifdef CONFIG_XENO_OPT_STATS
struct xnintr nktimer;	     /* Only for statistics */
static int xnintr_count = 1; /* Number of attached xnintr objects + nktimer */
static int xnintr_list_rev;  /* Modification counter of xnintr list */

/* Both functions update xnintr_list_rev at the very end.
 * This guarantees that module.c::stat_seq_open() won't get
 * an up-to-date xnintr_list_rev and old xnintr_count. */

static inline void stat_counter_inc(void)
{
	xnintr_count++;
	smp_mb();
	xnintr_list_rev++;
}

static inline void stat_counter_dec(void)
{
	xnintr_count--;
	smp_mb();
	xnintr_list_rev++;
}

static inline void sync_stat_references(struct xnintr *intr)
{
	struct xnirqstat *statp;
	struct xnsched *sched;
	int cpu;

	for_each_realtime_cpu(cpu) {
		sched = xnsched_struct(cpu);
		statp = per_cpu_ptr(intr->stats, cpu);
		/* Synchronize on all dangling references to go away. */
		while (sched->current_account == &statp->account)
			cpu_relax();
	}
}
#else
static inline void stat_counter_inc(void) {}
static inline void stat_counter_dec(void) {}
static inline void sync_stat_references(struct xnintr *intr) {}
#endif /* CONFIG_XENO_OPT_STATS */

static void xnintr_irq_handler(unsigned int irq, void *cookie);

void xnintr_host_tick(struct xnsched *sched) /* Interrupts off. */
{
	sched->lflags &= ~XNHTICK;
#ifdef XNARCH_HOST_TICK_IRQ
	ipipe_post_irq_root(XNARCH_HOST_TICK_IRQ);
#endif
}

/*
 * Low-level core clock irq handler. This one forwards ticks from the
 * Xenomai platform timer to nkclock exclusively.
 */
void xnintr_core_clock_handler(void)
{
	struct xnsched *sched = xnsched_current();
	int cpu  __maybe_unused = xnsched_cpu(sched);
	struct xnirqstat *statp;
	xnstat_exectime_t *prev;

	if (!xnsched_supported_cpu(cpu)) {
#ifdef XNARCH_HOST_TICK_IRQ
		ipipe_post_irq_root(XNARCH_HOST_TICK_IRQ);
#endif
		return;
	}

	statp = xnstat_percpu_data;
	prev = xnstat_exectime_switch(sched, &statp->account);
	xnstat_counter_inc(&statp->hits);

	trace_cobalt_clock_entry(per_cpu(ipipe_percpu.hrtimer_irq, cpu));

	++sched->inesting;
	sched->lflags |= XNINIRQ;

	xnlock_get(&nklock);
	xnclock_tick(&nkclock);
	xnlock_put(&nklock);

	trace_cobalt_clock_exit(per_cpu(ipipe_percpu.hrtimer_irq, cpu));
	xnstat_exectime_switch(sched, prev);

	if (--sched->inesting == 0) {
		sched->lflags &= ~XNINIRQ;
		xnsched_run();
		sched = xnsched_current();
	}
	/*
	 * If the core clock interrupt preempted a real-time thread,
	 * any transition to the root thread has already triggered a
	 * host tick propagation from xnsched_run(), so at this point,
	 * we only need to propagate the host tick in case the
	 * interrupt preempted the root thread.
	 */
	if ((sched->lflags & XNHTICK) &&
	    xnthread_test_state(sched->curr, XNROOT))
		xnintr_host_tick(sched);
}

/* Optional support for shared interrupts. */

#ifdef CONFIG_XENO_OPT_SHIRQ

struct xnintr_irq {
	DECLARE_XNLOCK(lock);
	struct xnintr *handlers;
	int unhandled;
} ____cacheline_aligned_in_smp;

static struct xnintr_irq xnirqs[IPIPE_NR_IRQS];

static inline struct xnintr *xnintr_shirq_first(unsigned int irq)
{
	return xnirqs[irq].handlers;
}

static inline struct xnintr *xnintr_shirq_next(struct xnintr *prev)
{
	return prev->next;
}

/*
 * Low-level interrupt handler dispatching the user-defined ISRs for
 * shared interrupts -- Called with interrupts off.
 */
static void xnintr_shirq_handler(unsigned int irq, void *cookie)
{
	struct xnsched *sched = xnsched_current();
	struct xnintr_irq *shirq = &xnirqs[irq];
	struct xnirqstat *statp;
	xnstat_exectime_t *prev;
	struct xnintr *intr;
	xnticks_t start;
	int s = 0, ret;

	prev  = xnstat_exectime_get_current(sched);
	start = xnstat_exectime_now();
	trace_cobalt_irq_entry(irq);

	++sched->inesting;
	sched->lflags |= XNINIRQ;

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
			statp = __this_cpu_ptr(intr->stats);
			xnstat_counter_inc(&statp->hits);
			xnstat_exectime_lazy_switch(sched, &statp->account, start);
			start = xnstat_exectime_now();
		}

		intr = intr->next;
	}

	xnlock_put(&shirq->lock);

	if (unlikely(s == XN_ISR_NONE)) {
		if (++shirq->unhandled == XNINTR_MAX_UNHANDLED) {
			printk(XENO_ERR "%s: IRQ%d not handled. Disabling IRQ line\n",
			       __FUNCTION__, irq);
			s |= XN_ISR_NOENABLE;
		}
	} else
		shirq->unhandled = 0;

	if (s & XN_ISR_PROPAGATE)
		ipipe_post_irq_root(irq);
	else if (!(s & XN_ISR_NOENABLE))
		ipipe_end_irq(irq);

	xnstat_exectime_switch(sched, prev);

	if (--sched->inesting == 0) {
		sched->lflags &= ~XNINIRQ;
		xnsched_run();
	}

	trace_cobalt_irq_exit(irq);
}

/*
 * Low-level interrupt handler dispatching the user-defined ISRs for
 * shared edge-triggered interrupts -- Called with interrupts off.
 */
static void xnintr_edge_shirq_handler(unsigned int irq, void *cookie)
{
	const int MAX_EDGEIRQ_COUNTER = 128;
	struct xnsched *sched = xnsched_current();
	struct xnintr_irq *shirq = &xnirqs[irq];
	int s = 0, counter = 0, ret, code;
	struct xnintr *intr, *end = NULL;
	struct xnirqstat *statp;
	xnstat_exectime_t *prev;
	xnticks_t start;

	prev  = xnstat_exectime_get_current(sched);
	start = xnstat_exectime_now();
	trace_cobalt_irq_entry(irq);

	++sched->inesting;
	sched->lflags |= XNINIRQ;

	xnlock_get(&shirq->lock);
	intr = shirq->handlers;

	while (intr != end) {
		statp = __this_cpu_ptr(intr->stats);
		xnstat_exectime_switch(sched, &statp->account);
		/*
		 * NOTE: We assume that no CPU migration will occur
		 * while running the interrupt service routine.
		 */
		ret = intr->isr(intr);
		code = ret & ~XN_ISR_BITMASK;
		s |= ret;

		if (code == XN_ISR_HANDLED) {
			end = NULL;
			xnstat_counter_inc(&statp->hits);
			xnstat_exectime_lazy_switch(sched, &statp->account, start);
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
		printk(XENO_ERR "%s: failed to get the IRQ%d line free\n",
		       __FUNCTION__, irq);

	if (unlikely(s == XN_ISR_NONE)) {
		if (++shirq->unhandled == XNINTR_MAX_UNHANDLED) {
			printk(XENO_ERR "%s: IRQ%d not handled. Disabling IRQ line\n",
			       __FUNCTION__, irq);
			s |= XN_ISR_NOENABLE;
		}
	} else
		shirq->unhandled = 0;

	if (s & XN_ISR_PROPAGATE)
		ipipe_post_irq_root(irq);
	else if (!(s & XN_ISR_NOENABLE))
		ipipe_end_irq(irq);

	xnstat_exectime_switch(sched, prev);

	if (--sched->inesting == 0) {
		sched->lflags &= ~XNINIRQ;
		xnsched_run();
	}

	trace_cobalt_irq_exit(irq);
}

static inline int xnintr_irq_attach(struct xnintr *intr)
{
	struct xnintr_irq *shirq = &xnirqs[intr->irq];
	struct xnintr *prev, **p = &shirq->handlers;
	int ret;

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

		ret = ipipe_request_irq(&xnsched_realtime_domain,
					intr->irq, handler, intr, (ipipe_irq_ackfn_t)intr->iack);
		if (ret)
			return ret;
	}

	intr->next = NULL;

	/* Add the given interrupt object. No need to synchronise with the IRQ
	   handler, we are only extending the chain. */
	*p = intr;

	return 0;
}

static inline void xnintr_irq_detach(struct xnintr *intr)
{
	struct xnintr_irq *shirq = &xnirqs[intr->irq];
	struct xnintr *e, **p = &shirq->handlers;

	while ((e = *p) != NULL) {
		if (e == intr) {
			/* Remove the given interrupt object from the list. */
			xnlock_get(&shirq->lock);
			*p = e->next;
			xnlock_put(&shirq->lock);

			sync_stat_references(intr);

			/* Release the IRQ line if this was the last user */
			if (shirq->handlers == NULL)
				ipipe_free_irq(&xnsched_realtime_domain, intr->irq);

			return;
		}
		p = &e->next;
	}

	printk(XENO_ERR "attempted to detach a non previously attached interrupt "
	       "object\n");
}

#else /* !CONFIG_XENO_OPT_SHIRQ */

#if defined(CONFIG_SMP) || XENO_DEBUG(LOCKING)
struct xnintr_irq {
	DECLARE_XNLOCK(lock);
} ____cacheline_aligned_in_smp;

static struct xnintr_irq xnirqs[IPIPE_NR_IRQS];
#endif /* CONFIG_SMP || XENO_DEBUG(LOCKING) */

static inline struct xnintr *xnintr_shirq_first(unsigned int irq)
{
	return __ipipe_irq_cookie(&xnsched_realtime_domain, irq);
}

static inline struct xnintr *xnintr_shirq_next(struct xnintr *prev)
{
	return NULL;
}

static inline int xnintr_irq_attach(struct xnintr *intr)
{
	return ipipe_request_irq(&xnsched_realtime_domain,
				 intr->irq, xnintr_irq_handler, intr,
				 (ipipe_irq_ackfn_t)intr->iack);
}

static inline void xnintr_irq_detach(struct xnintr *intr)
{
	int irq = intr->irq;

	xnlock_get(&xnirqs[irq].lock);
	ipipe_free_irq(&xnsched_realtime_domain, irq);
	xnlock_put(&xnirqs[irq].lock);

	sync_stat_references(intr);
}

#endif /* !CONFIG_XENO_OPT_SHIRQ */

/*
 * Low-level interrupt handler dispatching non-shared ISRs -- Called
 * with interrupts off.
 */
static void xnintr_irq_handler(unsigned int irq, void *cookie)
{
	struct xnsched *sched = xnsched_current();
	struct xnirqstat *statp;
	xnstat_exectime_t *prev;
	struct xnintr *intr;
	xnticks_t start;
	int s;

	prev  = xnstat_exectime_get_current(sched);
	start = xnstat_exectime_now();
	trace_cobalt_irq_entry(irq);

	++sched->inesting;
	sched->lflags |= XNINIRQ;

	xnlock_get(&xnirqs[irq].lock);

#ifdef CONFIG_SMP
	/*
	 * In SMP case, we have to reload the cookie under the per-IRQ
	 * lock to avoid racing with xnintr_detach.  However, we
	 * assume that no CPU migration will occur while running the
	 * interrupt service routine, so the scheduler pointer will
	 * remain valid throughout this function.
	 */
	intr = __ipipe_irq_cookie(&xnsched_realtime_domain, irq);
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
			printk(XENO_ERR "%s: IRQ%d not handled. Disabling IRQ line\n",
			       __FUNCTION__, irq);
			s |= XN_ISR_NOENABLE;
		}
	} else {
		statp = __this_cpu_ptr(intr->stats);
		xnstat_counter_inc(&statp->hits);
		xnstat_exectime_lazy_switch(sched, &statp->account, start);
		intr->unhandled = 0;
	}

#ifdef CONFIG_SMP
unlock_and_exit:
#endif
	xnlock_put(&xnirqs[irq].lock);

	if (s & XN_ISR_PROPAGATE)
		ipipe_post_irq_root(irq);
	else if (!(s & XN_ISR_NOENABLE))
		ipipe_end_irq(irq);

	xnstat_exectime_switch(sched, prev);

	if (--sched->inesting == 0) {
		sched->lflags &= ~XNINIRQ;
		xnsched_run();
	}

	trace_cobalt_irq_exit(irq);
}

int __init xnintr_mount(void)
{
	int i;
	for (i = 0; i < IPIPE_NR_IRQS; ++i)
		xnlock_init(&xnirqs[i].lock);
	return 0;
}

static void clear_irqstats(struct xnintr *intr)
{
	struct xnirqstat *p;
	int cpu;

	for_each_realtime_cpu(cpu) {
		p = per_cpu_ptr(intr->stats, cpu);
		memset(p, 0, sizeof(*p));
	}
}

/**
 * @fn int xnintr_init(struct xnintr *intr,const char *name,unsigned int irq,xnisr_t isr,xniack_t iack,int flags)
 * @brief Initialize an interrupt object.
 *
 * Associates an interrupt object with an IRQ line.
 *
 * When an interrupt occurs on the given @a irq line, the ISR is fired
 * in order to deal with the hardware event. The interrupt service
 * code may call any non-blocking service from the nucleus.
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
 * @warning Use these bits with care and only when you do understand
 * their effect on the system.  The ISR is not encouraged to use these
 * bits in case it shares the IRQ line with other ISRs in the
 * real-time domain.
 *
 * - XN_ISR_NOENABLE prevents the IRQ line from being re-enabled after
 * the ISR has returned.
 *
 * - XN_ISR_PROPAGATE causes the IRQ event to be propagated down the
 * pipeline to Linux. This is the regular way to share interrupts
 * between the nucleus and the regular Linux kernel. In effect,
 * XN_ISR_PROPAGATE implies XN_ISR_NOENABLE since it would make no
 * sense to re-enable the IRQ line before the Linux kernel had a
 * chance to process the propagated interrupt.
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
 * interrupt object or NULL.
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
 * - XN_ISR_EDGE is an additional flag need to be set together with
 * XN_ISR_SHARED to enable IRQ-sharing of edge-triggered interrupts.
 *
 * @return 0 is returned on success. Otherwise, -EINVAL is returned if
 * @a irq is not a valid interrupt number.
 *
 * @remark Tags: secondary-only.
 */

int xnintr_init(struct xnintr *intr, const char *name,
		unsigned int irq, xnisr_t isr, xniack_t iack,
		int flags)
{
	secondary_mode_only();

	if (irq >= IPIPE_NR_IRQS)
		return -EINVAL;

	intr->irq = irq;
	intr->isr = isr;
	intr->iack = iack;
	intr->cookie = NULL;
	intr->name = name ? : "<unknown>";
	intr->flags = flags;
	intr->unhandled = 0;
#ifdef CONFIG_XENO_OPT_SHIRQ
	intr->next = NULL;
#endif
	intr->stats = alloc_percpu(struct xnirqstat);
	clear_irqstats(intr);

	return 0;
}
EXPORT_SYMBOL_GPL(xnintr_init);

/**
 * @fn void xnintr_destroy(struct xnintr *intr)
 * @brief Destroy an interrupt object.
 *
 * Destroys an interrupt object previously initialized by
 * xnintr_init(). The interrupt object is automatically detached by a
 * call to xnintr_detach(). No more IRQs will be dispatched by this
 * object after this service has returned.
 *
 * @param intr The descriptor address of the interrupt object to
 * destroy.
 *
 * @remark Tags: secondary-only.
 */
void xnintr_destroy(struct xnintr *intr)
{
	secondary_mode_only();
	xnintr_detach(intr);
	free_percpu(intr->stats);
}
EXPORT_SYMBOL_GPL(xnintr_destroy);

/**
 * @fn int xnintr_attach(struct xnintr *intr, void *cookie)
 * @brief Attach an interrupt object.
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
 * @remark Tags: secondary-only.
 *
 * @note Attaching an interrupt resets the tracked number of receipts
 * to zero.
 */
int xnintr_attach(struct xnintr *intr, void *cookie)
{
	int ret;

	secondary_mode_only();

	intr->cookie = cookie;
	clear_irqstats(intr);

#ifdef CONFIG_SMP
	ipipe_set_irq_affinity(intr->irq, nkaffinity);
#endif /* CONFIG_SMP */

	mutex_lock(&intrlock);

	if (intr->flags & XN_ISR_ATTACHED) {
		ret = -EBUSY;
		goto out;
	}

	ret = xnintr_irq_attach(intr);
	if (ret)
		goto out;

	intr->flags |= XN_ISR_ATTACHED;
	stat_counter_inc();
out:
	mutex_unlock(&intrlock);

	return ret;
}
EXPORT_SYMBOL_GPL(xnintr_attach);

/**
 * @fn int xnintr_detach(struct xnintr *intr)
 * @brief Detach an interrupt object.
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
 * @note The caller <b>must not</b> hold nklock when invoking this
 * service, this would cause deadlocks.
 *
 * @remark Tags: secondary-only.
 */
void xnintr_detach(struct xnintr *intr)
{
	secondary_mode_only();

	mutex_lock(&intrlock);

	if (intr->flags & XN_ISR_ATTACHED) {
		intr->flags &= ~XN_ISR_ATTACHED;
		xnintr_irq_detach(intr);
		stat_counter_dec();
	}

	mutex_unlock(&intrlock);
}
EXPORT_SYMBOL_GPL(xnintr_detach);

/**
 * @fn void xnintr_enable(struct xnintr *intr)
 * @brief Enable an interrupt object.
 *
 * Enables the hardware interrupt line associated with an interrupt
 * object.
 *
 * @param intr The descriptor address of the interrupt object to
 * enable.
 *
 * @remark Tags: secondary-only.
 */

void xnintr_enable(struct xnintr *intr)
{
	secondary_mode_only();
	trace_cobalt_irq_enable(intr->irq);
	ipipe_enable_irq(intr->irq);
}
EXPORT_SYMBOL_GPL(xnintr_enable);

/**
 * @fn void xnintr_disable(struct xnintr *intr)
 * @brief Disable an interrupt object.
 *
 * Disables the hardware interrupt line associated with an interrupt
 * object. This operation invalidates further interrupt requests from
 * the given source until the IRQ line is re-enabled anew.
 *
 * @param intr The descriptor address of the interrupt object to
 * disable.
 *
 * @remark Tags: secondary-only.
 */

void xnintr_disable(struct xnintr *intr)
{
	secondary_mode_only();
	trace_cobalt_irq_disable(intr->irq);
	ipipe_disable_irq(intr->irq);
}
EXPORT_SYMBOL_GPL(xnintr_disable);

/**
 * @fn void xnintr_affinity(struct xnintr *intr, cpumask_t cpumask)
 * @brief Set interrupt's processor affinity.
 *
 * Restricts the IRQ associated with the interrupt object @a intr to
 * be received only on processors which bits are set in @a cpumask.
 *
 * @param intr The descriptor address of the interrupt object which
 * affinity is to be changed.
 *
 * @param cpumask The new processor affinity of the interrupt object.
 *
 * @note Depending on architectures, setting more than one bit in @a
 * cpumask could be meaningless.
 *
 * @remark Tags: secondary-only.
 */

void xnintr_affinity(struct xnintr *intr, cpumask_t cpumask)
{
	secondary_mode_only();
#ifdef CONFIG_SMP
	ipipe_set_irq_affinity(intr->irq, cpumask);
#endif
}
EXPORT_SYMBOL_GPL(xnintr_affinity);

static inline int xnintr_is_timer_irq(int irq)
{
	int cpu;

	for_each_realtime_cpu(cpu)
		if (irq == per_cpu(ipipe_percpu.hrtimer_irq, cpu))
			return 1;

	return 0;
}

#ifdef CONFIG_XENO_OPT_STATS

int xnintr_get_query_lock(void)
{
	return mutex_lock_interruptible(&intrlock) ? -ERESTARTSYS : 0;
}

void xnintr_put_query_lock(void)
{
	mutex_unlock(&intrlock);
}

int xnintr_query_init(struct xnintr_iterator *iterator)
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
	 * xnintr_query() will trigger an appropriate error below.
	 */
	iterator->list_rev = xnintr_list_rev;
	smp_mb();

	return xnintr_count;
}

int xnintr_query_next(int irq, struct xnintr_iterator *iterator,
		      char *name_buf)
{
	struct xnirqstat *statp;
	xnticks_t last_switch;
	struct xnintr *intr;
	int cpu;

	for (cpu = iterator->cpu + 1; cpu < num_present_cpus(); ++cpu) {
		if (cpu_online(cpu))
			break;
	}
	if (cpu == num_present_cpus())
		cpu = 0;
	iterator->cpu = cpu;

	if (iterator->list_rev != xnintr_list_rev)
		return -EAGAIN;

	if (!iterator->prev) {
		if (xnintr_is_timer_irq(irq))
			intr = &nktimer;
		else
			intr = xnintr_shirq_first(irq);
	} else
		intr = xnintr_shirq_next(iterator->prev);

	if (intr == NULL) {
		cpu = -1;
		iterator->prev = NULL;
		return -ENODEV;
	}

	ksformat(name_buf, XNOBJECT_NAME_LEN, "IRQ%d: %s", irq, intr->name);

	statp = per_cpu_ptr(intr->stats, cpu);
	iterator->hits = xnstat_counter_get(&statp->hits);
	last_switch = xnsched_struct(cpu)->last_account_switch;
	iterator->exectime_period = statp->account.total;
	iterator->account_period = last_switch - statp->account.start;
	statp->sum.total += iterator->exectime_period;
	iterator->exectime_total = statp->sum.total;
	statp->account.total = 0;
	statp->account.start = last_switch;

	/*
	 * Proceed to next entry in shared IRQ chain when all CPUs
	 * have been visited for this one.
	 */
	if (cpu + 1 == num_present_cpus())
		iterator->prev = intr;

	return 0;
}

#endif /* CONFIG_XENO_OPT_STATS */

#ifdef CONFIG_XENO_OPT_VFILE

#include <cobalt/kernel/vfile.h>

static inline int format_irq_proc(unsigned int irq,
				  struct xnvfile_regular_iterator *it)
{
	struct xnintr *intr;
	int cpu;

	for_each_realtime_cpu(cpu)
		if (xnintr_is_timer_irq(irq)) {
			xnvfile_printf(it, "         [timer/%d]", cpu);
			return 0;
		}

#ifdef CONFIG_SMP
	/*
	 * IPI numbers on ARM are not compile time constants, so do
	 * not use switch/case here.
	 */
	if (irq == IPIPE_HRTIMER_IPI) {
		xnvfile_puts(it, "         [timer-ipi]");
		return 0;
	}
	if (irq == IPIPE_RESCHEDULE_IPI) {
		xnvfile_puts(it, "         [reschedule]");
		return 0;
	}
	if (irq == IPIPE_CRITICAL_IPI) {
		xnvfile_puts(it, "         [sync]");
		return 0;
	}
#endif /* CONFIG_SMP */
	if (ipipe_virtual_irq_p(irq)) {
		xnvfile_puts(it, "         [virtual]");
		return 0;
	}

	mutex_lock(&intrlock);

	intr = xnintr_shirq_first(irq);
	if (intr) {
		xnvfile_puts(it, "        ");

		do {
			xnvfile_putc(it, ' ');
			xnvfile_puts(it, intr->name);
			intr = xnintr_shirq_next(intr);
		} while (intr);
	}

	mutex_unlock(&intrlock);

	return 0;
}

static int irq_vfile_show(struct xnvfile_regular_iterator *it,
			  void *data)
{
	int cpu, irq;

	/* FIXME: We assume the entire output fits in a single page. */

	xnvfile_puts(it, "  IRQ ");

	for_each_realtime_cpu(cpu)
		xnvfile_printf(it, "        CPU%d", cpu);

	for (irq = 0; irq < IPIPE_NR_IRQS; irq++) {
		if (__ipipe_irq_handler(&xnsched_realtime_domain, irq) == NULL)
			continue;

		xnvfile_printf(it, "\n%5d:", irq);

		for_each_realtime_cpu(cpu) {
			xnvfile_printf(it, "%12lu",
				       __ipipe_cpudata_irq_hits(&xnsched_realtime_domain, cpu,
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

void xnintr_init_proc(void)
{
	xnvfile_init_regular("irq", &irq_vfile, &nkvfroot);
}

void xnintr_cleanup_proc(void)
{
	xnvfile_destroy_regular(&irq_vfile);
}

#endif /* CONFIG_XENO_OPT_VFILE */

/*@}*/

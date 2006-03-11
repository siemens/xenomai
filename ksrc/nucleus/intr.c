/*!\file
 * \brief Interrupt management.
 * \author Philippe Gerum
 *
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
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

#define XENO_INTR_MODULE 1

#include <nucleus/pod.h>
#include <nucleus/intr.h>
#include <nucleus/ltt.h>

xnintr_t nkclock;

static void xnintr_irq_handler(unsigned irq,
			       void *cookie);

#if defined(CONFIG_XENO_OPT_SHIRQ_LEVEL) || defined(CONFIG_XENO_OPT_SHIRQ_EDGE)

/* Helper functions. */
static int xnintr_shirq_attach(xnintr_t *intr, void *cookie);
static int xnintr_shirq_detach(xnintr_t *intr);

#endif /* CONFIG_XENO_OPT_SHIRQ_LEVEL || CONFIG_XENO_OPT_SHIRQ_EDGE */

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
 * - XN_ISR_PROPAGATE tells the nucleus to require the real-time control
 * layer to forward the IRQ. For instance, this would cause the Adeos
 * control layer to propagate the interrupt down the interrupt
 * pipeline to other Adeos domains, such as Linux. This is the regular
 * way to share interrupts between the nucleus and the host system.
 *
 * - XN_ISR_NOENABLE causes the nucleus to ask the real-time control
 * layer _not_ to re-enable the IRQ line (read the following section).
 * xnarch_end_irq() must be called to re-enable the IRQ line later.
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
 * interrupt object.
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
 * @return No error condition being defined, 0 is always returned.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

int xnintr_init (xnintr_t *intr,
		 const char *name,
		 unsigned irq,
		 xnisr_t isr,
		 xniack_t iack,
		 xnflags_t flags)
{
    intr->irq = irq;
    intr->isr = isr;
    intr->iack = iack;
    intr->cookie = NULL;
    intr->hits = 0;
    intr->name = name;
    intr->flags = flags;
#if defined(CONFIG_XENO_OPT_SHIRQ_LEVEL) || defined(CONFIG_XENO_OPT_SHIRQ_EDGE)
    intr->next = NULL;
#endif /* CONFIG_XENO_OPT_SHIRQ_LEVEL || CONFIG_XENO_OPT_SHIRQ_EDGE */

    return 0;
}

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
 * @return 0 is returned on success. Otherwise, -EBUSY is returned if
 * an error occurred while detaching the interrupt (see
 * xnintr_detach()).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

int xnintr_destroy (xnintr_t *intr)

{
    xnintr_detach(intr);
    return 0;
}

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
 * @return 0 is returned on success. Otherwise, -EINVAL is returned if
 * a low-level error occurred while attaching the interrupt. -EBUSY is
 * specifically returned if the interrupt object was already attached.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 *
 * @note Attaching an interrupt resets the tracked number of receipts
 * to zero.
 */

int xnintr_attach (xnintr_t *intr,
		   void *cookie)
{
    intr->hits = 0;
    intr->cookie = cookie;
#if defined(CONFIG_XENO_OPT_SHIRQ_LEVEL) || defined(CONFIG_XENO_OPT_SHIRQ_EDGE)
    return xnintr_shirq_attach(intr,cookie);
#else /* !CONFIG_XENO_OPT_SHIRQ_LEVEL && !CONFIG_XENO_OPT_SHIRQ_EDGE */
    return xnarch_hook_irq(intr->irq,&xnintr_irq_handler,intr->iack,intr);
#endif /* CONFIG_XENO_OPT_SHIRQ_LEVEL || CONFIG_XENO_OPT_SHIRQ_EDGE */
}

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
 * @return 0 is returned on success. Otherwise, -EINVAL is returned if
 * a low-level error occurred while detaching the interrupt. Detaching
 * a non-attached interrupt object leads to a null-effect and returns
 * 0.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

int xnintr_detach (xnintr_t *intr)

{
#if defined(CONFIG_XENO_OPT_SHIRQ_LEVEL) || defined(CONFIG_XENO_OPT_SHIRQ_EDGE)
    return xnintr_shirq_detach(intr);
#else /* !CONFIG_XENO_OPT_SHIRQ_LEVEL && !CONFIG_XENO_OPT_SHIRQ_EDGE */
    return xnarch_release_irq(intr->irq);
#endif /* CONFIG_XENO_OPT_SHIRQ_LEVEL || CONFIG_XENO_OPT_SHIRQ_EDGE */
}

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
 * - User-space task
 *
 * Rescheduling: never.
 */

int xnintr_enable (xnintr_t *intr)

{
    return xnarch_enable_irq(intr->irq);
}

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
 * - User-space task
 *
 * Rescheduling: never.
 */

int xnintr_disable (xnintr_t *intr)

{
    return xnarch_disable_irq(intr->irq);
}

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

xnarch_cpumask_t xnintr_affinity (xnintr_t *intr, xnarch_cpumask_t cpumask)

{
    return xnarch_set_irq_affinity(intr->irq,cpumask);
}

/* Low-level clock irq handler. */

void xnintr_clock_handler (void)

{
    xnarch_announce_tick();
    xnintr_irq_handler(nkclock.irq,&nkclock);
}

/*
 * Low-level interrupt handler dispatching the user-defined ISR for
 * interrupts other than the clock IRQ -- Called with interrupts off.
 */

static void xnintr_irq_handler (unsigned irq, void *cookie)

{
    xnsched_t *sched = xnpod_current_sched();
    xnintr_t *intr = (xnintr_t *)cookie;
    int s;

    xnarch_memory_barrier();

    xnltt_log_event(xeno_ev_ienter,irq);

    ++sched->inesting;
    s = intr->isr(intr);
    ++intr->hits;

    if (s & XN_ISR_PROPAGATE)
	xnarch_chain_irq(irq);
    else if (!(s & XN_ISR_NOENABLE))
	xnarch_end_irq(irq);

    if (--sched->inesting == 0 && xnsched_resched_p())
	xnpod_schedule();

    /* Since the host tick is low priority, we can wait for returning
       from the rescheduling procedure before actually calling the
       propagation service, if it is pending. */

    if (testbits(sched->status,XNHTICK))
	{
	__clrbits(sched->status,XNHTICK);
	xnarch_relay_tick();
	}

    xnltt_log_event(xeno_ev_iexit,irq);
}

/*@}*/

/* Optional support for shared interrupts. */

#if defined(CONFIG_XENO_OPT_SHIRQ_LEVEL) || defined(CONFIG_XENO_OPT_SHIRQ_EDGE)

typedef struct xnintr_shirq {

    xnintr_t *handlers;
#ifdef CONFIG_SMP
    atomic_counter_t active;
#endif /* CONFIG_SMP */

} xnintr_shirq_t;

static xnintr_shirq_t xnshirqs[RTHAL_NR_IRQS];

#ifdef CONFIG_SMP
static inline void xnintr_shirq_lock(xnintr_shirq_t *shirq) {
    xnarch_atomic_inc(&shirq->active);
}

static inline void xnintr_shirq_unlock(xnintr_shirq_t *shirq) {
    xnarch_atomic_dec(&shirq->active);
}

static inline void xnintr_shirq_spin(xnintr_shirq_t *shirq) {
    while (xnarch_atomic_get(&shirq->active))
	cpu_relax();
}
#else /* !CONFIG_SMP */
static inline void xnintr_shirq_lock(xnintr_shirq_t *shirq) {}
static inline void xnintr_shirq_unlock(xnintr_shirq_t *shirq) {}
static inline void xnintr_shirq_spin(xnintr_shirq_t *shirq) {}
#endif /* CONFIG_SMP */

#if defined(CONFIG_XENO_OPT_SHIRQ_LEVEL)

/*
 * Low-level interrupt handler dispatching the user-defined ISRs for
 * shared interrupts -- Called with interrupts off.
 */

static void xnintr_shirq_handler (unsigned irq, void *cookie)
{
    xnsched_t *sched = xnpod_current_sched();
    xnintr_shirq_t *shirq = &xnshirqs[irq];
    xnintr_t *intr;
    int s = 0;

    xnarch_memory_barrier();

    xnltt_log_event(xeno_ev_ienter,irq);

    ++sched->inesting;

    xnintr_shirq_lock(shirq);
    intr = shirq->handlers;

    while (intr)
        {
	s |= intr->isr(intr) & XN_ISR_BITMASK;
        ++intr->hits;
        intr = intr->next;
        }
    xnintr_shirq_unlock(shirq);

    if (s & XN_ISR_PROPAGATE)
	xnarch_chain_irq(irq);
    else if (!(s & XN_ISR_NOENABLE))
	xnarch_end_irq(irq);

    if (--sched->inesting == 0 && xnsched_resched_p())
	xnpod_schedule();

    xnltt_log_event(xeno_ev_iexit,irq);
}

#endif /* CONFIG_XENO_OPT_SHIRQ_LEVEL */

#if defined(CONFIG_XENO_OPT_SHIRQ_EDGE)

/*
 * Low-level interrupt handler dispatching the user-defined ISRs for
 * shared edge-triggered interrupts -- Called with interrupts off.
 */

static void xnintr_edge_shirq_handler (unsigned irq, void *cookie)
{
    const int MAX_EDGEIRQ_COUNTER = 128;

    xnsched_t *sched = xnpod_current_sched();
    xnintr_shirq_t *shirq = &xnshirqs[irq];
    xnintr_t *intr, *end = NULL;
    int s = 0, counter = 0;

    xnarch_memory_barrier();

    xnltt_log_event(xeno_ev_ienter,irq);

    ++sched->inesting;

    xnintr_shirq_lock(shirq);
    intr = shirq->handlers;

    while (intr != end)
	{
	int ret = intr->isr(intr),
	    code = ret & ~XN_ISR_BITMASK,
	    bits = ret & XN_ISR_BITMASK;

	if (code == XN_ISR_HANDLED)
	    {
	    ++intr->hits;
	    end = NULL;
	    s |= bits;	    
	    }
	else if (code == XN_ISR_NONE && end == NULL)
	    end = intr;

	if (counter++ > MAX_EDGEIRQ_COUNTER)
	    break;

	if (!(intr = intr->next))
	    intr = shirq->handlers;
	}

    xnintr_shirq_unlock(shirq);

    if (counter > MAX_EDGEIRQ_COUNTER)
	xnlogerr("xnintr_edge_shirq_handler() : failed to get the IRQ%d line free.\n", irq);

    if (s & XN_ISR_PROPAGATE)
	xnarch_chain_irq(irq);
    else if (!(s & XN_ISR_NOENABLE))
	xnarch_end_irq(irq);

    if (--sched->inesting == 0 && xnsched_resched_p())
	xnpod_schedule();

    xnltt_log_event(xeno_ev_iexit,irq);
}

#endif /* CONFIG_XENO_OPT_SHIRQ_EDGE */

static int xnintr_shirq_attach (xnintr_t *intr,
			        void *cookie)
{
    xnintr_shirq_t *shirq = &xnshirqs[intr->irq];
    xnintr_t *prev, **p = &shirq->handlers;
    unsigned long flags;
    int err = 0;

    if (intr->irq >= RTHAL_NR_IRQS)
	return -EINVAL;

    flags = rthal_critical_enter(NULL);

    if (__testbits(intr->flags,XN_ISR_ATTACHED))
	{
	err = -EPERM;
	goto unlock_and_exit;
	}

    if ((prev = *p) != NULL)
	{
	/* Check on whether the shared mode is allowed. */
	if (!(prev->flags & intr->flags & XN_ISR_SHARED) || (prev->iack != intr->iack)
	    || ((prev->flags & XN_ISR_EDGE) != (intr->flags & XN_ISR_EDGE)))
	    {
	    err = -EBUSY;
	    goto unlock_and_exit;
	    }

	/* Get a position at the end of the list to insert the new element. */
	while (prev) 
	    {
	    p = &prev->next;
	    prev = *p;
	    }
	}
    else
	{
	/* Initialize the corresponding interrupt channel */
	void (*handler)(unsigned, void *) = &xnintr_irq_handler;

	if (intr->flags & XN_ISR_SHARED)
	    {
#if defined(CONFIG_XENO_OPT_SHIRQ_LEVEL)
	    handler = &xnintr_shirq_handler;
#endif /* CONFIG_XENO_OPT_SHIRQ_LEVEL */

#if defined(CONFIG_XENO_OPT_SHIRQ_EDGE)
	    if (intr->flags & XN_ISR_EDGE)
		handler = &xnintr_edge_shirq_handler;
#endif /* CONFIG_XENO_OPT_SHIRQ_EDGE */
	    }

	err = xnarch_hook_irq(intr->irq,handler,intr->iack,intr);
	if (err)
	    goto unlock_and_exit;
	}

    __setbits(intr->flags,XN_ISR_ATTACHED);

    /* Add a given interrupt object. */
    intr->next = NULL;
    *p = intr;

unlock_and_exit:

    rthal_critical_exit(flags);
    return err;
}

int xnintr_shirq_detach (xnintr_t *intr)
{
    xnintr_shirq_t *shirq = &xnshirqs[intr->irq];
    xnintr_t *e, **p = &shirq->handlers;
    unsigned long flags;
    int err = 0;

    if (intr->irq >= RTHAL_NR_IRQS)
	return -EINVAL;

    flags = rthal_critical_enter(NULL);

    if (!__testbits(intr->flags,XN_ISR_ATTACHED))
	{
	rthal_critical_exit(flags);
	return -EPERM;
	}

    __clrbits(intr->flags,XN_ISR_ATTACHED);

    while ((e = *p) != NULL)
	{
	if (e == intr)
	    {
	    /* Remove a given interrupt object from the list. */
	    *p = e->next;

	    /* Release the IRQ line if this was the last user */
	    if (shirq->handlers == NULL)
		err = xnarch_release_irq(intr->irq);

	    rthal_critical_exit(flags);

	    /* The idea here is to keep a detached interrupt object valid as long
	       as the corresponding irq handler is running. This is one of the requirements
	       to iterate over the xnintr_shirq_t::handlers list in xnintr_irq_handler()
	       in a lockless way. */

	    xnintr_shirq_spin(shirq);
	    return err;
	    }
	p = &e->next;
	}

    rthal_critical_exit(flags);

    xnlogerr("Attempted to detach a non previously attached interrupt object");
    return err;
}

int xnintr_mount(void)
{
    int i;
    for (i = 0; i < RTHAL_NR_IRQS; ++i)
	{
	xnshirqs[i].handlers = NULL;
#ifdef CONFIG_SMP
	atomic_set(&xnshirqs[i].active,0);
#endif /* CONFIG_SMP */
	}
    return 0;
}

int xnintr_irq_proc(unsigned int irq, char *str)
{
    xnintr_shirq_t *shirq;
    xnintr_t *intr;
    char *p = str;

    if (rthal_virtual_irq_p(irq))
	{
	p += sprintf(p, "         [virtual]");
	return p - str;
	}
    else if (irq == XNARCH_TIMER_IRQ)
	{
	p += sprintf(p, "         %s", nkclock.name);
	return p - str;
	}

    shirq = &xnshirqs[irq];

    xnintr_shirq_lock(shirq);
    intr = shirq->handlers;

    if (intr)
	p += sprintf(p, "        ");

    while (intr)
	{
	if (*(intr->name))
	    p += sprintf(p, " %s,", intr->name);

	intr = intr->next;
	}

    xnintr_shirq_unlock(shirq);

    if (p != str)
	--p;

    return p - str;
}

#else /* !CONFIG_XENO_OPT_SHIRQ_LEVEL && !CONFIG_XENO_OPT_SHIRQ_EDGE */

int xnintr_mount(void)
{
    return 0;
}

int xnintr_irq_proc(unsigned int irq, char *str)
{
    return 0;
}

#endif /* CONFIG_XENO_OPT_SHIRQ_LEVEL || CONFIG_XENO_OPT_SHIRQ_EDGE */

EXPORT_SYMBOL(xnintr_attach);
EXPORT_SYMBOL(xnintr_destroy);
EXPORT_SYMBOL(xnintr_detach);
EXPORT_SYMBOL(xnintr_disable);
EXPORT_SYMBOL(xnintr_enable);
EXPORT_SYMBOL(xnintr_affinity);
EXPORT_SYMBOL(xnintr_init);

/*!\file pod.h
 * \brief Real-time pod interface header.
 * \author Philippe Gerum
 *
 * Copyright (C) 2001-2013 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) 2004 The RTAI project <http://www.rtai.org>
 * Copyright (C) 2004 The HYADES project <http://www.hyades-itea.org>
 * Copyright (C) 2004 The Xenomai project <http://www.xenomai.org>
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
 * \ingroup pod
 */

#ifndef _COBALT_KERNEL_POD_H
#define _COBALT_KERNEL_POD_H

/*! \addtogroup pod
 *@{*/

#include <cobalt/kernel/sched.h>
#include <cobalt/kernel/shadow.h>
#include <cobalt/kernel/lock.h>

/* Pod status flags */
#define XNCLKLK  0x00000001	/* All clocks locked */

#define XNPOD_NORMAL_EXIT  0x0
#define XNPOD_FATAL_EXIT   0x1

#define XNPOD_ALL_CPUS  CPU_MASK_ALL

#define XNPOD_FATAL_BUFSZ  16384

#define nkpod (&nkpod_struct)

struct xnsynch;

/*!
 * \brief Real-time pod descriptor.
 *
 * The source of all Xenomai magic.
 */

struct xnpod {
	unsigned long status;	  /*!< Status bitmask. */
	struct list_head threadq; /*!< All existing threads. */
	int nrthreads;
#ifdef CONFIG_XENO_OPT_VFILE
	struct xnvfile_rev_tag threadlist_tag;
#endif
	atomic_t timerlck;	/*!< Timer lock depth.  */
};

typedef struct xnpod xnpod_t;

DECLARE_EXTERN_XNLOCK(nklock);

extern unsigned long nktimerlat;

extern cpumask_t nkaffinity;

extern struct xnpod nkpod_struct;

extern struct xnpersonality generic_personality;

void __xnpod_cleanup_thread(struct xnthread *thread);

#ifdef CONFIG_XENO_HW_FPU
void xnpod_switch_fpu(struct xnsched *sched);
#else
static inline void xnpod_switch_fpu(struct xnsched *sched) { }
#endif /* CONFIG_XENO_HW_FPU */

void __xnpod_schedule(struct xnsched *sched);

void __xnpod_schedule_handler(void);

static inline struct xnsched *xnpod_sched_slot(int cpu)
{
	return &per_cpu(nksched, cpu);
}

static inline struct xnsched *xnpod_current_sched(void)
{
	/* IRQs off */
	return __this_cpu_ptr(&nksched);
}

static inline int xnpod_interrupt_p(void)
{
	return xnpod_current_sched()->lflags & XNINIRQ;
}

static inline struct xnthread *xnpod_current_thread(void)
{
	return xnpod_current_sched()->curr;
}

static inline int xnpod_locked_p(void)
{
	return xnthread_test_state(xnpod_current_thread(), XNLOCK);
}

static inline int xnpod_root_p(void)
{
	return xnthread_test_state(xnpod_current_thread(), XNROOT);
}

static inline int xnpod_unblockable_p(void)
{
	return xnpod_interrupt_p() || xnpod_root_p();
}

static inline int xnpod_primary_p(void)
{
	return !xnpod_unblockable_p();
}

int xnpod_init(void);

int xnpod_enable_timesource(void);

void xnpod_disable_timesource(void);

void xnpod_shutdown(int xtype);

int xnpod_init_thread(struct xnthread *thread,
		      const struct xnthread_init_attr *attr,
		      struct xnsched_class *sched_class,
		      const union xnsched_policy_param *sched_param);

int xnpod_start_thread(struct xnthread *thread,
		       const struct xnthread_start_attr *attr);

void xnpod_cancel_thread(struct xnthread *thread);

void xnpod_join_thread(struct xnthread *thread);

int xnpod_set_thread_mode(struct xnthread *thread,
			  int clrmask,
			  int setmask);

void xnpod_suspend_thread(struct xnthread *thread,
			  int mask,
			  xnticks_t timeout,
			  xntmode_t timeout_mode,
			  struct xnsynch *wchan);

void xnpod_resume_thread(struct xnthread *thread,
			 int mask);

int xnpod_unblock_thread(struct xnthread *thread);

int xnpod_set_thread_schedparam(struct xnthread *thread,
				struct xnsched_class *sched_class,
				const union xnsched_policy_param *sched_param);

int xnpod_migrate_thread(int cpu);

static inline void xnpod_schedule(void)
{
	struct xnsched *sched;
	/*
	 * NOTE: Since __xnpod_schedule() won't run if an escalation
	 * to primary domain is needed, we won't use critical
	 * scheduler information before we actually run in primary
	 * mode; therefore we can first test the scheduler status then
	 * escalate.  Running in the primary domain means that no
	 * Linux-triggered CPU migration may occur from that point
	 * either. Finally, since migration is always a self-directed
	 * operation for Xenomai threads, we can safely read the
	 * scheduler state bits without holding the nklock.
	 *
	 * Said differently, if we race here because of a CPU
	 * migration, it must have been Linux-triggered because we run
	 * in secondary mode; in which case we will escalate to the
	 * primary domain, then unwind the current call frame without
	 * running the rescheduling procedure in
	 * __xnpod_schedule(). Therefore, the scheduler pointer will
	 * be either valid, or unused.
	 */
	sched = xnpod_current_sched();
	smp_rmb();
	/*
	 * No immediate rescheduling is possible if an ISR context is
	 * active, or if we are caught in the middle of a unlocked
	 * context switch.
	 */
#if XENO_DEBUG(NUCLEUS)
	if ((sched->status|sched->lflags) &
	    (XNINIRQ|XNINSW|XNINLOCK))
		return;
#else /* !XENO_DEBUG(NUCLEUS) */
	if (((sched->status|sched->lflags) &
	     (XNINIRQ|XNINSW|XNRESCHED|XNINLOCK)) != XNRESCHED)
		return;
#endif /* !XENO_DEBUG(NUCLEUS) */

	__xnpod_schedule(sched);
}

void ___xnpod_lock_sched(struct xnsched *sched);

void ___xnpod_unlock_sched(struct xnsched *sched);

static inline void __xnpod_lock_sched(void)
{
	struct xnsched *sched;

	barrier();
	sched = xnpod_current_sched();
	___xnpod_lock_sched(sched);
}

static inline void __xnpod_unlock_sched(void)
{
	struct xnsched *sched;

	barrier();
	sched = xnpod_current_sched();
	___xnpod_unlock_sched(sched);
}

static inline void xnpod_lock_sched(void)
{
	struct xnsched *sched;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	sched = xnpod_current_sched();
	___xnpod_lock_sched(sched);
	xnlock_put_irqrestore(&nklock, s);
}

static inline void xnpod_unlock_sched(void)
{
	struct xnsched *sched;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	sched = xnpod_current_sched();
	___xnpod_unlock_sched(sched);
	xnlock_put_irqrestore(&nklock, s);
}

void __xnpod_testcancel_thread(struct xnthread *curr);

/**
 * @fn void xnpod_testcancel_thread(void)
 *
 * @brief Introduce a thread cancellation point.
 *
 * Terminates the current thread if a cancellation request is pending
 * for it, i.e. if xnpod_cancel_thread() was called.
 *
 * Calling context: This service may be called from all runtime modes
 * of kernel or user-space threads.
 */
static inline void xnpod_testcancel_thread(void)
{
	struct xnthread *curr = xnshadow_current();

	if (curr && xnthread_test_info(curr, XNCANCELD))
		__xnpod_testcancel_thread(curr);
}

int xnpod_handle_exception(struct ipipe_trap_data *d);

int xnpod_set_thread_periodic(struct xnthread *thread,
			      xnticks_t idate,
			      xntmode_t timeout_mode,
			      xnticks_t period);

int xnpod_wait_thread_period(unsigned long *overruns_r);

int xnpod_set_thread_tslice(struct xnthread *thread,
			    xnticks_t quantum);

static inline void xnpod_yield(void)
{
	xnpod_resume_thread(xnpod_current_thread(), 0);
	xnpod_schedule();
}

static inline void xnpod_delay(xnticks_t timeout)
{
	xnpod_suspend_thread(xnpod_current_thread(), XNDELAY, timeout, XN_RELATIVE, NULL);
}

static inline void xnpod_suspend_self(void)
{
	xnpod_suspend_thread(xnpod_current_thread(), XNSUSP, XN_INFINITE, XN_RELATIVE, NULL);
}

static inline void xnpod_delete_self(void)
{
	xnpod_cancel_thread(xnpod_current_thread());
}

/*@}*/

#endif /* !_COBALT_KERNEL_POD_H */

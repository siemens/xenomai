/*!\file sched.h
 * \brief Scheduler interface header.
 * \author Philippe Gerum
 *
 * Copyright (C) 2008 Philippe Gerum <rpm@xenomai.org>.
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
 * \ingroup sched
 */

#ifndef _XENO_NUCLEUS_SCHED_H
#define _XENO_NUCLEUS_SCHED_H

/*! \addtogroup sched
 *@{*/

#include <nucleus/thread.h>

#if defined(__KERNEL__) || defined(__XENO_SIM__)

#include <nucleus/schedqueue.h>
#include <nucleus/sched-tp.h>

/* Sched status flags */
#define XNKCOUT	 0x80000000	/* Sched callout context */
#define XNHTICK  0x40000000	/* Host tick pending  */
#define XNRPICK  0x20000000	/* Check RPI state */
#define XNINTCK  0x10000000	/* In master tick handler context */
#define XNINIRQ  0x08000000	/* In IRQ handling context */
#define XNSWLOCK 0x04000000	/* In context switch */

#define XNSCHED_EVT_DEADLINE	24 /* Deadline event (thread->signals). */

struct xnsched_rt {

	xnsched_queue_t runnable;	/*!< Runnable thread queue. */
#ifdef CONFIG_XENO_OPT_PRIOCPL
	xnsched_queue_t relaxed;	/*!< Relaxed thread queue. */
#endif /* CONFIG_XENO_OPT_PRIOCPL */
};

/*! 
 * \brief Scheduling information structure.
 */

typedef struct xnsched {

	xnflags_t status;		/*!< Scheduler specific status bitmask. */
	struct xnthread *curr;		/*!< Current thread. */
	xnarch_cpumask_t resched;	/*!< Mask of CPUs needing rescheduling. */

	struct xnsched_rt rt;		/*!< Context of built-in real-time class. */
#ifdef CONFIG_XENO_OPT_SCHED_TP
	struct xnsched_tp tp;		/*!< Context of TP class. */
#endif

	xntimerq_t timerqueue;		/* !< Core timer queue. */
	volatile unsigned inesting;	/*!< Interrupt nesting level. */
	struct xntimer htimer;		/*!< Host timer. */
	struct xnthread *zombie;
	struct xnthread rootcb;		/*!< Root thread control block. */

#ifdef CONFIG_XENO_HW_UNLOCKED_SWITCH
	struct xnthread *last;
#endif

#ifdef CONFIG_XENO_HW_FPU
	struct xnthread *fpuholder;	/*!< Thread owning the current FPU context. */
#endif

#ifdef CONFIG_XENO_OPT_WATCHDOG
	struct xntimer wdtimer;	/*!< Watchdog timer object. */
	int wdcount;		/*!< Watchdog tick count. */
#endif

#ifdef CONFIG_XENO_OPT_STATS
	xnticks_t last_account_switch;	/*!< Last account switch date (ticks). */
	xnstat_exectime_t *current_account;	/*!< Currently active account */
#endif

#ifdef CONFIG_XENO_OPT_PRIOCPL
	DECLARE_XNLOCK(rpilock);	/*!< RPI lock */
#endif

#ifdef CONFIG_XENO_OPT_PERVASIVE
	struct task_struct *gatekeeper;
	wait_queue_head_t gkwaitq;
	struct linux_semaphore gksync;
	struct xnthread *gktarget;
#endif

} xnsched_t;

union xnsched_policy_param;

struct xnsched_class {

	void (*sched_init)(struct xnsched *sched);
	void (*sched_enqueue)(struct xnthread *thread);
	void (*sched_dequeue)(struct xnthread *thread);
	void (*sched_requeue)(struct xnthread *thread);
	struct xnthread *(*sched_pick)(struct xnsched *sched);
	void (*sched_tick)(struct xnthread *curr);
	void (*sched_rotate)(struct xnsched *sched, int prio);
	void (*sched_migrate)(struct xnthread *thread,
			      struct xnsched *sched);
	void (*sched_setparam)(struct xnthread *thread,
			       const union xnsched_policy_param *p);
	void (*sched_getparam)(struct xnthread *thread,
			       union xnsched_policy_param *p);
	void (*sched_trackprio)(struct xnthread *thread,
				const union xnsched_policy_param *p);
	void (*sched_forget)(struct xnthread *thread);
#ifdef CONFIG_XENO_OPT_PRIOCPL
	struct xnthread *(*sched_push_rpi)(struct xnsched *sched,
					   struct xnthread *thread);
	void (*sched_pop_rpi)(struct xnthread *thread);
	struct xnthread *(*sched_peek_rpi)(struct xnsched *sched);
#endif
	struct xnsched_class *next;
	int weight;
	const char *name;
};

#define XNSCHED_CLASS_MAX_THREADS	32768
#define XNSCHED_CLASS_WEIGHT(n)		(n * XNSCHED_CLASS_MAX_THREADS)

/* Placeholder for current thread priority */
#define XNSCHED_RUNPRIO   0x80000000

#ifdef CONFIG_SMP
#define xnsched_cpu(__sched__)	((__sched__) - &nkpod->sched[0])
#else /* !CONFIG_SMP */
#define xnsched_cpu(__sched__)	({ (void)__sched__; 0; })
#endif /* CONFIG_SMP */

/* Test all resched flags from the given scheduler mask. */
#define xnsched_resched_p(__sched__)			\
  (!xnarch_cpus_empty((__sched__)->resched))

/* Set self resched flag for the given scheduler. */
#define xnsched_set_self_resched(__sched__)		\
  xnarch_cpu_set(xnsched_cpu(__sched__), (__sched__)->resched)

/* Set specific resched flag into the local scheduler mask. */
#define xnsched_set_resched(__sched__)			\
    xnarch_cpu_set(xnsched_cpu(__sched__), xnpod_current_sched()->resched)

void xnsched_zombie_hooks(struct xnthread *thread);

#ifdef CONFIG_XENO_HW_UNLOCKED_SWITCH

struct xnsched *xnsched_finish_unlocked_switch(struct xnsched *sched);

void xnsched_resched_after_unlocked_switch(void);

#else /* !CONFIG_XENO_HW_UNLOCKED_SWITCH */

#ifdef CONFIG_SMP
#define xnsched_finish_unlocked_switch(__sched__)	xnpod_current_sched()
#else /* !CONFIG_SMP */
#define xnsched_finish_unlocked_switch(__sched__)	(__sched__)
#endif /* !CONFIG_SMP */

#define xnsched_resched_after_unlocked_switch()		do { } while(0)

#endif /* !CONFIG_XENO_HW_UNLOCKED_SWITCH */

#ifdef CONFIG_XENO_OPT_WATCHDOG
static inline void xnsched_reset_watchdog(struct xnsched *sched)
{
	sched->wdcount = 0;
}
#else /* !CONFIG_XENO_OPT_WATCHDOG */
static inline void xnsched_reset_watchdog(struct xnsched *sched)
{
}
#endif /* CONFIG_XENO_OPT_WATCHDOG */

#include <nucleus/sched-builtin.h>

void xnsched_init(struct xnsched *sched);

void xnsched_destroy(struct xnsched *sched);

struct xnthread *xnsched_pick_next(struct xnsched *sched);

void xnsched_putback(struct xnthread *thread);

void xnsched_set_policy(struct xnthread *thread,
			struct xnsched_class *sched_class,
			const union xnsched_policy_param *p);

void xnsched_track_policy(struct xnthread *thread,
			  struct xnthread *target);

void xnsched_migrate(struct xnthread *thread,
		     struct xnsched *sched);

void xnsched_migrate_passive(struct xnthread *thread,
			     struct xnsched *sched);

void xnsched_rotate(int prio);

static inline void xnsched_init_tcb(struct xnthread *thread)
{
	xnsched_idle_init_tcb(thread);
	xnsched_rt_init_tcb(thread);
#ifdef CONFIG_XENO_OPT_SCHED_TP
	xnsched_tp_init_tcb(thread);
#endif /* CONFIG_XENO_OPT_SCHED_TP */
}

static inline int xnsched_root_priority(struct xnsched *sched)
{
	return sched->rootcb.cprio;
}

static inline struct xnsched_class *xnsched_root_class(struct xnsched *sched)
{
	return sched->rootcb.sched_class;
}

static inline void xnsched_tick(struct xnthread *curr)
{
	struct xnsched_class *sched_class = curr->sched_class;

	if (sched_class != &xnsched_class_idle)
		sched_class->sched_tick(curr);
}

#ifdef CONFIG_XENO_OPT_SCHED_CLASSES

static inline void xnsched_enqueue(struct xnthread *thread)
{
	struct xnsched_class *sched_class = thread->sched_class;

	if (sched_class != &xnsched_class_idle)
		thread->sched_class->sched_enqueue(thread);
}

static inline void xnsched_dequeue(struct xnthread *thread)
{
	struct xnsched_class *sched_class = thread->sched_class;

	if (sched_class != &xnsched_class_idle)
		thread->sched_class->sched_dequeue(thread);
}

static inline void xnsched_requeue(struct xnthread *thread)
{
	struct xnsched_class *sched_class = thread->sched_class;

	if (sched_class != &xnsched_class_idle)
		sched_class->sched_requeue(thread);
}

static inline int xnsched_weighted_bprio(struct xnthread *thread)
{
	return thread->bprio + thread->sched_class->weight;
}

static inline int xnsched_weighted_cprio(struct xnthread *thread)
{
	return thread->cprio + thread->sched_class->weight;
}

static inline void xnsched_setparam(struct xnthread *thread,
				    const union xnsched_policy_param *p)
{
	thread->sched_class->sched_setparam(thread, p);
}

static inline void xnsched_getparam(struct xnthread *thread,
				    union xnsched_policy_param *p)
{
	thread->sched_class->sched_getparam(thread, p);
}

static inline void xnsched_trackprio(struct xnthread *thread,
				     const union xnsched_policy_param *p)
{
	thread->sched_class->sched_trackprio(thread, p);
}

static inline void xnsched_forget(struct xnthread *thread)
{
	struct xnsched_class *sched_class = thread->sched_class;

	if (sched_class->sched_forget)
		sched_class->sched_forget(thread);
}

#ifdef CONFIG_XENO_OPT_PRIOCPL

static inline struct xnthread *xnsched_push_rpi(struct xnsched *sched,
						struct xnthread *thread)
{
	return thread->sched_class->sched_push_rpi(sched, thread);
}

static inline void xnsched_pop_rpi(struct xnthread *thread)
{
	thread->sched_class->sched_pop_rpi(thread);
}

#endif /* CONFIG_XENO_OPT_PRIOCPL */

#else /* !CONFIG_XENO_OPT_SCHED_CLASSES */

/*
 * If only the RT scheduling class is compiled in, we may fully inline
 * common helpers for it.
 */

static inline void xnsched_enqueue(struct xnthread *thread)
{
	struct xnsched_class *sched_class = thread->sched_class;

	if (sched_class != &xnsched_class_idle)
		__xnsched_rt_enqueue(thread);
}

static inline void xnsched_dequeue(struct xnthread *thread)
{
	struct xnsched_class *sched_class = thread->sched_class;

	if (sched_class != &xnsched_class_idle)
		__xnsched_rt_dequeue(thread);
}

static inline void xnsched_requeue(struct xnthread *thread)
{
	struct xnsched_class *sched_class = thread->sched_class;

	if (sched_class != &xnsched_class_idle)
		__xnsched_rt_requeue(thread);
}

static inline int xnsched_weighted_bprio(struct xnthread *thread)
{
	return thread->bprio;
}

static inline int xnsched_weighted_cprio(struct xnthread *thread)
{
	return thread->cprio;
}

static inline void xnsched_setparam(struct xnthread *thread,
				    const union xnsched_policy_param *p)
{
	struct xnsched_class *sched_class = thread->sched_class;

	if (sched_class != &xnsched_class_idle)
		__xnsched_rt_setparam(thread, p);
	else
		__xnsched_idle_setparam(thread, p);
}

static inline void xnsched_getparam(struct xnthread *thread,
				    union xnsched_policy_param *p)
{
	struct xnsched_class *sched_class = thread->sched_class;

	if (sched_class != &xnsched_class_idle)
		__xnsched_rt_getparam(thread, p);
	else
		__xnsched_idle_getparam(thread, p);
}

static inline void xnsched_trackprio(struct xnthread *thread,
				     const union xnsched_policy_param *p)
{
	struct xnsched_class *sched_class = thread->sched_class;

	if (sched_class != &xnsched_class_idle)
		__xnsched_rt_trackprio(thread, p);
	else
		__xnsched_idle_trackprio(thread, p);
}

static inline void xnsched_forget(struct xnthread *thread)
{
}

#ifdef CONFIG_XENO_OPT_PRIOCPL

static inline struct xnthread *xnsched_push_rpi(struct xnsched *sched,
						struct xnthread *thread)
{
	return __xnsched_rt_push_rpi(sched, thread);
}

static inline void xnsched_pop_rpi(struct xnthread *thread)
{
	__xnsched_rt_pop_rpi(thread);
}

#endif /* CONFIG_XENO_OPT_PRIOCPL */

#endif /* !CONFIG_XENO_OPT_SCHED_CLASSES */

void xnsched_renice_root(struct xnsched *sched,
			 struct xnthread *target);

struct xnthread *xnsched_peek_rpi(struct xnsched *sched);

#else /* !(__KERNEL__ || __XENO_SIM__) */

#include <nucleus/sched-builtin.h>

#endif /* !(__KERNEL__ || __XENO_SIM__) */

/*@}*/

#endif /* !_XENO_NUCLEUS_SCHED_H */

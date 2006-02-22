/*!\file pod.h
 * \brief Real-time pod interface header.
 * \author Philippe Gerum
 *
 * Copyright (C) 2001,2002,2003,2004,2005 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _XENO_NUCLEUS_POD_H
#define _XENO_NUCLEUS_POD_H

/*! \addtogroup pod
 *@{*/

#include <nucleus/thread.h>
#include <nucleus/intr.h>

/* Creation flags */
#define XNDREORD 0x00000001     /* Don't reorder pend queues upon prio change */

/* Pod status flags */
#define XNRPRIO  0x00000002     /* Reverse priority scheme */
#define XNTIMED  0x00000004     /* Timer started */
#define XNTMSET  0x00000008     /* Pod time has been set */
#define XNTMPER  0x00000010     /* Periodic timing */
#define XNFATAL  0x00000020     /* Pod encountered a fatal error */
#define XNPIDLE  0x00000040     /* Pod is unavailable (initializing/shutting down) */
#define XNTLOCK  0x00000080	/* Timer lock pending */

/* Sched status flags */
#define XNKCOUT  0x80000000	/* Sched callout context */
#define XNHTICK  0x40000000	/* Host tick pending  */

/* These flags are available to the real-time interfaces */
#define XNPOD_SPARE0  0x01000000
#define XNPOD_SPARE1  0x02000000
#define XNPOD_SPARE2  0x04000000
#define XNPOD_SPARE3  0x08000000
#define XNPOD_SPARE4  0x10000000
#define XNPOD_SPARE5  0x20000000
#define XNPOD_SPARE6  0x40000000
#define XNPOD_SPARE7  0x80000000

/* Flags for context checking */
#define XNPOD_THREAD_CONTEXT     0x1 /* Regular thread */
#define XNPOD_INTERRUPT_CONTEXT  0x2 /* Interrupt service thread */
#define XNPOD_HOOK_CONTEXT       0x4 /* Nanokernel hook */
#define XNPOD_ROOT_CONTEXT       0x8 /* Root thread */

#define XNPOD_NORMAL_EXIT  0x0
#define XNPOD_FATAL_EXIT   0x1

#define XNPOD_DEFAULT_TICKHANDLER  (&xnpod_announce_tick)

#define XNPOD_ALL_CPUS  XNARCH_CPU_MASK_ALL

#define XNPOD_HEAPSIZE  (CONFIG_XENO_OPT_SYS_HEAPSZ * 1024)
#define XNPOD_PAGESIZE  512
#define XNPOD_RUNPRIO   0x80000000 /* Placeholder for "stdthread priority" */

/* Flags for xnpod_schedule_runnable() */
#define XNPOD_SCHEDFIFO 0x0
#define XNPOD_SCHEDLIFO 0x1
#define XNPOD_NOSWITCH  0x2

/* Normal root thread priority == min_std_prio - 1 */
#define XNPOD_ROOT_PRIO_BASE   ((nkpod)->root_prio_base)

#ifdef CONFIG_XENO_OPT_SCALABLE_SCHED
typedef xnspqueue_t xnsched_queue_t;
#define sched_initpq    initspq
#define sched_countpq   countspq
#define sched_insertpql insertspql
#define sched_insertpqf insertspqf
#define sched_appendpq  appendspq
#define sched_prependpq prependspq
#define sched_removepq  removespq
#define sched_getheadpq getheadspq
#define sched_getpq     getspq
#define sched_findpqh   findspqh
#else /* ! CONFIG_XENO_OPT_SCALABLE_SCHED */
typedef xnpqueue_t xnsched_queue_t;
#define sched_initpq    initpq
#define sched_countpq   countpq
#define sched_insertpql insertpql
#define sched_insertpqf insertpqf
#define sched_appendpq  appendpq
#define sched_prependpq prependpq
#define sched_removepq  removepq
#define sched_getheadpq getheadpq
#define sched_getpq     getpq
#define sched_findpqh   findpqh
#endif /* !CONFIG_XENO_OPT_SCALABLE_SCHED */

#define XNPOD_FATAL_BUFSZ  16384

/*! 
 * \brief Scheduling information structure.
 */

typedef struct xnsched {

    xnflags_t status;           /*!< Scheduler specific status bitmask */

    xnthread_t *runthread;      /*!< Current thread (service or user). */

    xnarch_cpumask_t resched;   /*!< Mask of CPUs needing rescheduling.*/

    xnsched_queue_t readyq;     /*!< Ready-to-run threads (prioritized). */

    xntimerq_t timerqueue;
#ifdef CONFIG_XENO_OPT_TIMING_PERIODIC
    xnqueue_t timerwheel [XNTIMER_WHEELSIZE]; /*!< BSDish timer wheel. */
#endif /* CONFIG_XENO_OPT_TIMING_PERIODIC */

    volatile unsigned inesting; /*!< Interrupt nesting level. */

#ifdef CONFIG_XENO_HW_FPU
    xnthread_t *fpuholder;      /*!< Thread owning the current FPU context. */
#endif /* CONFIG_XENO_HW_FPU */

    xnthread_t rootcb;          /*!< Root thread control block. */

} xnsched_t;

#ifdef CONFIG_SMP
#define xnsched_cpu(__sched__)                  \
    ((__sched__) - &nkpod->sched[0])
#else /* !CONFIG_SMP */
#define xnsched_cpu(__sched__) (0)
#endif /* CONFIG_SMP */

#define xnsched_resched_mask() \
    (xnpod_current_sched()->resched)

#define xnsched_resched_p()                     \
    (!xnarch_cpus_empty(xnsched_resched_mask()))

#define xnsched_tst_resched(__sched__) \
    xnarch_cpu_isset(xnsched_cpu(__sched__), xnsched_resched_mask())

#define xnsched_set_resched(__sched__) \
    xnarch_cpu_set(xnsched_cpu(__sched__), xnsched_resched_mask())

#define xnsched_clr_resched(__sched__) \
    xnarch_cpu_clear(xnsched_cpu(__sched__), xnsched_resched_mask())

#define xnsched_clr_mask(__sched__) \
    xnarch_cpus_clear((__sched__)->resched)

struct xnsynch;
struct xnintr;

/*! 
 * \brief Real-time pod descriptor.
 *
 * The source of all Xenomai magic.
 */

struct xnpod {

    xnflags_t status;           /*!< Status bitmask. */

    xnticks_t jiffies;          /*!< Periodic ticks elapsed since boot. */

    xnticks_t wallclock_offset; /*!< Difference between wallclock time
                                  and epoch in ticks. */

    xntimer_t htimer;           /*!< Host timer. */

    xnsched_t sched[XNARCH_NR_CPUS]; /*!< Per-cpu scheduler slots. */

    xnqueue_t suspendq;         /*!< Suspended (blocked) threads. */

    xnqueue_t threadq;          /*!< All existing threads. */

    volatile u_long schedlck;	/*!< Scheduler lock count. */

    xnqueue_t tstartq,          /*!< Thread start hook queue. */
              tswitchq,         /*!< Thread switch hook queue. */
              tdeleteq;         /*!< Thread delete hook queue. */

    int minpri,                 /*!< Minimum priority value. */
        maxpri;                 /*!< Maximum priority value. */

    int root_prio_base;         /*!< Base priority of ROOT thread. */

    u_long tickvalue;           /*!< Tick duration (ns, 1 if aperiodic). */

    u_long ticks2sec;		/*!< Number of ticks per second (1e9
                                  if aperiodic). */

    int refcnt;			/*!< Reference count.  */

#ifdef __KERNEL__
    atomic_counter_t timerlck;	/*!< Timer lock depth.  */
#endif /* __KERNEL__ */

    struct {
        void (*settime)(xnticks_t newtime); /*!< Clock setting hook. */
        int (*faulthandler)(xnarch_fltinfo_t *fltinfo); /*!< Trap/exception handler. */
        int (*unload)(void);    /*!< Unloading hook. */
    } svctable;                 /*!< Table of overridable service entry points. */

#ifdef CONFIG_XENO_OPT_WATCHDOG
    xnticks_t watchdog_trigger; /* !< Watchdog trigger value. */
    xnticks_t watchdog_reload;  /* !< Watchdog reload value. */
    int watchdog_armed;         /* !< Watchdog state. */
#endif /* CONFIG_XENO_OPT_WATCHDOG */

#ifdef __XENO_SIM__
    void (*schedhook)(xnthread_t *thread,
                      xnflags_t mask); /*!< Internal scheduling hook. */
#endif /* __XENO_SIM__ */
};

typedef struct xnpod xnpod_t;

extern xnpod_t *nkpod;

#ifdef CONFIG_SMP
extern xnlock_t nklock;
#endif /* CONFIG_SMP */

extern u_long nkschedlat;

extern u_long nktimerlat;

extern u_long nktickdef;

extern char *nkmsgbuf;

#define xnprintf(fmt,args...)  xnarch_printf(fmt , ##args)
#define xnloginfo(fmt,args...) xnarch_loginfo(fmt , ##args)
#define xnlogwarn(fmt,args...) xnarch_logwarn(fmt , ##args)
#define xnlogerr(fmt,args...)  xnarch_logerr(fmt , ##args)

#ifdef __cplusplus
extern "C" {
#endif

void xnpod_schedule_runnable(xnthread_t *thread,
                             int flags);

void xnpod_renice_thread_inner(xnthread_t *thread,
                               int prio,
                               int propagate);

#ifdef CONFIG_XENO_HW_FPU
void xnpod_switch_fpu(xnsched_t *sched);
#endif /* CONFIG_XENO_HW_FPU */

#ifdef CONFIG_XENO_OPT_WATCHDOG
static inline void xnpod_reset_watchdog (void)
{
    nkpod->watchdog_trigger = xnarch_get_cpu_tsc() + nkpod->watchdog_reload;
    nkpod->watchdog_armed = 0;
}
#else /* !CONFIG_XENO_OPT_WATCHDOG */
static inline void xnpod_reset_watchdog (void)
{
}
#endif /* CONFIG_XENO_OPT_WATCHDOG */

static inline int xnpod_get_qdir (xnpod_t *pod)
{
    /* Returns the queuing direction of threads for a given pod */
    return testbits(pod->status,XNRPRIO) ? xnqueue_up : xnqueue_down;
}

static inline int xnpod_get_minprio (xnpod_t *pod, int incr)
{
    return xnpod_get_qdir(pod) == xnqueue_up ?
        pod->minpri + incr :
        pod->minpri - incr;
}

static inline int xnpod_get_maxprio (xnpod_t *pod, int incr)
{
    return xnpod_get_qdir(pod) == xnqueue_up ?
        pod->maxpri - incr :
        pod->maxpri + incr;
}

static inline int xnpod_priocompare (int inprio, int outprio)
{
    /* Returns a negative, null or positive value whether inprio is
       lower than, equal to or greater than outprio. */
    int delta = inprio - outprio;
    return testbits(nkpod->status,XNRPRIO) ? -delta : delta;
}

static inline void xnpod_renice_root (int prio)

{
    xnthread_t *rootcb;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);
    rootcb = &nkpod->sched[xnarch_current_cpu()].rootcb;
    rootcb->cprio = prio;
    xnpod_schedule_runnable(rootcb,XNPOD_SCHEDLIFO|XNPOD_NOSWITCH);
    xnlock_put_irqrestore(&nklock,s);
}

    /* -- Beginning of the exported interface */

#define xnpod_sched_slot(cpu) \
    (&nkpod->sched[cpu])

#define xnpod_current_sched() \
    xnpod_sched_slot(xnarch_current_cpu())

#define xnpod_interrupt_p() \
    (xnpod_current_sched()->inesting > 0)

#define xnpod_callout_p() \
    (!!testbits(xnpod_current_sched()->status,XNKCOUT))

#define xnpod_asynch_p() \
    (xnpod_interrupt_p() || xnpod_callout_p())

#define xnpod_current_thread() \
    (xnpod_current_sched()->runthread)

#define xnpod_current_root() \
    (&xnpod_current_sched()->rootcb)

#define xnpod_current_p(thread) \
    (xnpod_current_thread() == (thread))

#define xnpod_locked_p() \
    (!!testbits(xnpod_current_thread()->status,XNLOCK))

#define xnpod_unblockable_p() \
    (xnpod_asynch_p() || testbits(xnpod_current_thread()->status,XNLOCK|XNROOT))

#define xnpod_root_p() \
    (!!testbits(xnpod_current_thread()->status,XNROOT))

#define xnpod_shadow_p() \
    (!!testbits(xnpod_current_thread()->status,XNSHADOW))

#define xnpod_userspace_p() \
    (!!testbits(xnpod_current_thread()->status,XNROOT|XNSHADOW))

#define xnpod_primary_p() \
    (!(xnpod_asynch_p() || xnpod_root_p()))

#define xnpod_secondary_p() \
    (xnpod_root_p())

#define xnpod_idle_p() xnpod_root_p()

#define xnpod_timeset_p() \
    (!!testbits(nkpod->status,XNTMSET))

static inline u_long xnpod_get_ticks2sec (void) {
    return nkpod->ticks2sec;
}

static inline u_long xnpod_get_tickval (void) {
    /* Returns the duration of a tick in nanoseconds */
    return nkpod->tickvalue;
}

static inline xntime_t xnpod_ticks2ns (xnticks_t ticks) {
    /* Convert a count of ticks in nanoseconds */
#ifdef CONFIG_XENO_OPT_TIMING_PERIODIC
    return ticks * xnpod_get_tickval();
#else /* !CONFIG_XENO_OPT_TIMING_PERIODIC */
    return ticks;
#endif /* !CONFIG_XENO_OPT_TIMING_PERIODIC */
}

static inline xnticks_t xnpod_ns2ticks (xntime_t t) {
#ifdef CONFIG_XENO_OPT_TIMING_PERIODIC
    return xnarch_ulldiv(t,xnpod_get_tickval(),NULL);
#else /* !CONFIG_XENO_OPT_TIMING_PERIODIC */
    return t;
#endif /* !CONFIG_XENO_OPT_TIMING_PERIODIC */
}

int xnpod_init(xnpod_t *pod,
               int minpri,
               int maxpri,
               xnflags_t flags);

int xnpod_start_timer(u_long nstick,
                      xnisr_t tickhandler);

void xnpod_stop_timer(void);

void xnpod_shutdown(int xtype);

int xnpod_init_thread(xnthread_t *thread,
                      const char *name,
                      int prio,
                      xnflags_t flags,
                      unsigned stacksize);

int xnpod_start_thread(xnthread_t *thread,
                       xnflags_t mode,
                       int imask,
                       xnarch_cpumask_t affinity,
                       void (*entry)(void *cookie),
                       void *cookie);

void xnpod_restart_thread(xnthread_t *thread);

void xnpod_delete_thread(xnthread_t *thread);

xnflags_t xnpod_set_thread_mode(xnthread_t *thread,
                                xnflags_t clrmask,
                                xnflags_t setmask);

void xnpod_suspend_thread(xnthread_t *thread,
                          xnflags_t mask,
                          xnticks_t timeout,
                          struct xnsynch *resource);

void xnpod_resume_thread(xnthread_t *thread,
                         xnflags_t mask);

int xnpod_unblock_thread(xnthread_t *thread);

void xnpod_renice_thread(xnthread_t *thread,
                         int prio);

int xnpod_migrate_thread(int cpu);

void xnpod_rotate_readyq(int prio);

void xnpod_schedule(void);

void xnpod_dispatch_signals(void);

static inline void xnpod_lock_sched (void)
{
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    if (nkpod->schedlck++ == 0)
	__setbits(xnpod_current_sched()->runthread->status,XNLOCK);

    xnlock_put_irqrestore(&nklock,s);
}

static inline void xnpod_unlock_sched (void)
{
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    if (--nkpod->schedlck == 0)
        {
        __clrbits(xnpod_current_sched()->runthread->status,XNLOCK);
        xnpod_schedule();
        }

    xnlock_put_irqrestore(&nklock,s);
}

int xnpod_announce_tick(struct xnintr *intr);

void xnpod_activate_rr(xnticks_t quantum);

void xnpod_deactivate_rr(void);

void xnpod_set_time(xnticks_t newtime);

int xnpod_set_thread_periodic(xnthread_t *thread,
                              xnticks_t idate,
                              xnticks_t period);

int xnpod_wait_thread_period(void);

xnticks_t xnpod_get_time(void);

static inline xntime_t xnpod_get_cpu_time(void)
{
    return xnarch_get_cpu_time();
}

int xnpod_add_hook(int type,
                   void (*routine)(xnthread_t *));

int xnpod_remove_hook(int type,
                      void (*routine)(xnthread_t *));

void xnpod_check_context(int mask);

static inline void xnpod_yield (void) {
    xnpod_resume_thread(xnpod_current_thread(),0);
    xnpod_schedule();
}

static inline void xnpod_delay (xnticks_t timeout) {
    xnpod_suspend_thread(xnpod_current_thread(),XNDELAY,timeout,NULL);
}

static inline void xnpod_suspend_self (void) {
    xnpod_suspend_thread(xnpod_current_thread(),XNSUSP,XN_INFINITE,NULL);
}

static inline void xnpod_delete_self (void) {
    xnpod_delete_thread(xnpod_current_thread());
}

#ifdef __cplusplus
}
#endif

/*@}*/

#endif /* !_XENO_NUCLEUS_POD_H */

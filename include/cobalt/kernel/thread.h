/*
 * @note Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
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
 * \ingroup thread
 */

#ifndef _COBALT_KERNEL_THREAD_H
#define _COBALT_KERNEL_THREAD_H

#include <cobalt/kernel/types.h>

/*! @ingroup nucleus
  @defgroup nucleus_state_flags Thread state flags.
  @brief Bits reporting permanent or transient states of thread.
  @{
*/

/* State flags */

#define XNSUSP    0x00000001 /**< Suspended. */
#define XNPEND    0x00000002 /**< Sleep-wait for a resource. */
#define XNDELAY   0x00000004 /**< Delayed */
#define XNREADY   0x00000008 /**< Linked to the ready queue. */
#define XNDORMANT 0x00000010 /**< Not started yet or killed */
#define XNZOMBIE  0x00000020 /**< Zombie thread in deletion process */
#define XNSTARTED 0x00000080 /**< Thread has been started */
#define XNMAPPED  0x00000100 /**< Thread is mapped to a linux task */
#define XNRELAX   0x00000200 /**< Relaxed shadow thread (blocking bit) */
#define XNMIGRATE 0x00000400 /**< Thread is currently migrating to another CPU. */
#define XNHELD    0x00000800 /**< Thread is held to process emergency. */

#define XNBOOST   0x00001000 /**< Undergoes a PIP boost */
#define XNDEBUG   0x00002000 /**< Hit a debugger breakpoint */
#define XNLOCK    0x00004000 /**< Holds the scheduler lock (i.e. not preemptible) */
#define XNRRB     0x00008000 /**< Undergoes a round-robin scheduling */
#define XNASDI    0x00010000 /**< ASR are disabled */
#define XNTRAPSW  0x00020000 /**< Trap execution mode switches */
#define XNFPU     0x00040000 /**< Thread uses FPU */
#define XNROOT    0x00080000 /**< Root thread (that is, Linux/IDLE) */
#define XNWEAK    0x00100000 /**< Non real-time shadow (from the WEAK class) */
#define XNUSER    0x00200000 /**< Shadow thread running in userland */

/*! @} */ /* Ends doxygen comment group: nucleus_state_flags */

/*
  Must follow the declaration order of the above bits. Status symbols
  are defined as follows:
  'S' -> Forcibly suspended.
  'w'/'W' -> Waiting for a resource, with or without timeout.
  'D' -> Delayed (without any other wait condition).
  'R' -> Runnable.
  'U' -> Unstarted or dormant.
  'X' -> Relaxed shadow.
  'H' -> Held in emergency.
  'b' -> Priority boost undergoing.
  'T' -> Ptraced and stopped.
  'l' -> Locks scheduler.
  'r' -> Undergoes round-robin.
  't' -> Mode switches trapped.
*/
#define XNTHREAD_STATE_LABELS  "SWDRU....X.HbTlr...t....."

#define XNTHREAD_BLOCK_BITS   (XNSUSP|XNPEND|XNDELAY|XNDORMANT|XNRELAX|XNMIGRATE|XNHELD)
#define XNTHREAD_MODE_BITS    (XNLOCK|XNRRB|XNASDI|XNTRAPSW)

/* These state flags are available to the real-time interfaces */
#define XNTHREAD_STATE_SPARE0  0x10000000
#define XNTHREAD_STATE_SPARE1  0x20000000
#define XNTHREAD_STATE_SPARE2  0x40000000
#define XNTHREAD_STATE_SPARE3  0x80000000
#define XNTHREAD_STATE_SPARES  0xf0000000

/*! @ingroup nucleus
  @defgroup nucleus_info_flags Thread information flags.
  @brief Bits reporting events notified to the thread.
  @{
*/

/* Information flags */

#define XNTIMEO   0x00000001 /**< Woken up due to a timeout condition */
#define XNRMID    0x00000002 /**< Pending on a removed resource */
#define XNBREAK   0x00000004 /**< Forcibly awaken from a wait state */
#define XNKICKED  0x00000008 /**< Forced out of primary mode */
#define XNWAKEN   0x00000010 /**< Thread waken up upon resource availability */
#define XNROBBED  0x00000020 /**< Robbed from resource ownership */
#define XNAFFSET  0x00000040 /**< CPU affinity changed from primary mode */
#define XNCANCELD 0x00000080 /**< Cancellation request is pending */
#define XNSWREP   0x00000100 /**< Mode switch already reported */

/* These information flags are available to the real-time interfaces */
#define XNTHREAD_INFO_SPARE0  0x10000000
#define XNTHREAD_INFO_SPARE1  0x20000000
#define XNTHREAD_INFO_SPARE2  0x40000000
#define XNTHREAD_INFO_SPARE3  0x80000000
#define XNTHREAD_INFO_SPARES  0xf0000000

/*! @} */ /* Ends doxygen comment group: nucleus_info_flags */

/*!
  @brief Structure containing thread information.
*/
typedef struct xnthread_info {

	unsigned long state; /**< Thread state, @see nucleus_state_flags */

	int bprio;  /**< Base priority. */
	int cprio; /**< Current priority. May change through Priority Inheritance.*/

	int cpu; /**< CPU the thread currently runs on. */
	unsigned long affinity; /**< Thread's CPU affinity. */

	unsigned long long relpoint; /**< Time of next release.*/

	unsigned long long exectime; /**< Execution time in primary mode in nanoseconds. */

	unsigned long modeswitches; /**< Number of primary->secondary mode switches. */
	unsigned long ctxswitches; /**< Number of context switches. */
	unsigned long pagefaults; /**< Number of triggered page faults. */
	unsigned long syscalls; /**< Number of Xenomai syscalls. */

	char name[XNOBJECT_NAME_LEN];  /**< Symbolic name assigned at creation. */

} xnthread_info_t;

struct xnthread_user_window {
	unsigned long state;
	unsigned long grant_value;
};

#ifdef __KERNEL__

#include <cobalt/kernel/list.h>
#include <cobalt/kernel/stat.h>
#include <cobalt/kernel/timer.h>
#include <cobalt/kernel/registry.h>
#include <cobalt/kernel/schedparam.h>
#include <asm/xenomai/machine.h>
#include <asm/xenomai/thread.h>

#define XNTHREAD_INVALID_ASR  ((void (*)(xnsigmask_t))0)

struct xnthread;
struct xnsynch;
struct xnsched;
struct xnselector;
struct xnsched_class;
struct xnsched_tpslot;
union xnsched_policy_param;
struct xnbufd;
struct xnpersonality;

struct xnthread_init_attr {
	struct xnpersonality *personality;
	xnflags_t flags;
	const char *name;
};

struct xnthread_start_attr {
	xnflags_t mode;
	cpumask_t affinity;
	void (*entry)(void *cookie);
	void *cookie;
};

struct xnthread_wait_context {
	/* anchor object */
};

typedef void (*xnasr_t)(xnsigmask_t sigs);

typedef struct xnthread {

	struct xnarchtcb tcb;		/* Architecture-dependent block -- Must be first */

	xnflags_t state;		/* Thread state flags */

	xnflags_t info;			/* Thread information flags */

	struct xnsched *sched;		/* Thread scheduler */

	struct xnsched_class *sched_class; /* Current scheduling class */

	struct xnsched_class *base_class; /* Base scheduling class */

#ifdef CONFIG_XENO_OPT_SCHED_TP
	struct xnsched_tpslot *tps;	/* Current partition slot for TP scheduling */
	struct list_head tp_link;	/* Link in per-sched TP thread queue */
#endif
#ifdef CONFIG_XENO_OPT_SCHED_SPORADIC
	struct xnsched_sporadic_data *pss; /* Sporadic scheduling data. */
#endif

	unsigned int idtag;	/* Unique ID tag */

	cpumask_t affinity;	/* Processor affinity. */

	int bprio;		/* Base priority (before PIP boost) */

	int cprio;		/* Current priority */

	/**
	 * Weighted priority (cprio + scheduling class weight).
	 */
	int wprio;

	u_long schedlck;	/** Scheduler lock count. */

	/**
	 * Thread holder in xnsched runnable queue. Prioritized by
	 * thread->cprio.
	 */
	struct list_head rlink;

	/**
	 * Thread holder in xnsynch pendq. Prioritized by
	 * thread->cprio + scheduling class weight.
	 */
	struct list_head plink;

	xnholder_t glink;		/* Thread holder in global queue */

#define link2thread(ln, fld)	container_of(ln, struct xnthread, fld)

	/**
	 * List of xnsynch owned by this thread _and_ claimed by
	 * others (PIP).
	 */
	struct list_head claimq;

	struct xnsynch *wchan;		/* Resource the thread pends on */

	struct xnsynch *wwake;		/* Wait channel the thread was resumed from */

	int hrescnt;			/* Held resources count */

	xntimer_t rtimer;		/* Resource timer */

	xntimer_t ptimer;		/* Periodic timer */

	xntimer_t rrbtimer;		/* Round-robin timer */

	xnsigmask_t signals;		/* Pending core signals */

	xnticks_t rrperiod;		/* Allotted round-robin period (ns) */

  	struct xnthread_wait_context *wcontext;	/* Active wait context. */

	struct {
		xnstat_counter_t ssw;	/* Primary -> secondary mode switch count */
		xnstat_counter_t csw;	/* Context switches (includes secondary -> primary switches) */
		xnstat_counter_t xsc;	/* Xenomai syscalls */
		xnstat_counter_t pf;	/* Number of page faults */
		xnstat_exectime_t account; /* Execution time accounting entity */
		xnstat_exectime_t lastperiod; /* Interval marker for execution time reports */
	} stat;

	struct xnselector *selector;    /* For select. */

	xnasr_t asr;			/* Asynchronous service routine */

	xnflags_t asrmode;		/* Thread's mode for ASR */

	int asrimask;			/* Thread's interrupt mask for ASR */

	unsigned asrlevel;		/* ASR execution level (ASRs are reentrant) */

	int imode;			/* Initial mode */

	struct xnsched_class *init_class; /* Initial scheduling class */

	union xnsched_policy_param init_schedparam; /* Initial scheduling parameters */

	struct {
		xnhandle_t handle;	/* Handle in registry */
		const char *waitkey;	/* Pended key */
	} registry;

	char name[XNOBJECT_NAME_LEN]; /* Symbolic name of thread */

	void (*entry)(void *cookie); /* Thread entry routine */

	void *cookie;		/* Cookie to pass to the entry routine */

	struct pt_regs *regs;		/* Current register frame */
	struct xnthread_user_window *u_window;	/* Data visible from userland. */

	void *privdata;				/* Private data for extension */

	struct xnpersonality *personality; /* Originating interface/personality */

#ifdef CONFIG_XENO_OPT_DEBUG
	const char *exe_path;	/* Executable path */
	u32 proghash;		/* Hash value for exe_path */
#endif
} xnthread_t;

#define xnthread_name(thread)               ((thread)->name)
#define xnthread_clear_name(thread)        do { *(thread)->name = 0; } while(0)
#define xnthread_sched(thread)             ((thread)->sched)
#define xnthread_start_time(thread)        ((thread)->stime)
#define xnthread_state_flags(thread)       ((thread)->state)
#define xnthread_test_state(thread,flags)  testbits((thread)->state,flags)
#define xnthread_set_state(thread,flags)   __setbits((thread)->state,flags)
#define xnthread_clear_state(thread,flags) __clrbits((thread)->state,flags)
#define xnthread_test_info(thread,flags)   testbits((thread)->info,flags)
#define xnthread_set_info(thread,flags)    __setbits((thread)->info,flags)
#define xnthread_clear_info(thread,flags)  __clrbits((thread)->info,flags)
#define xnthread_lock_count(thread)        ((thread)->schedlck)
#define xnthread_init_schedparam(thread)   ((thread)->init_schedparam)
#define xnthread_base_priority(thread)     ((thread)->bprio)
#define xnthread_current_priority(thread)  ((thread)->cprio)
#define xnthread_init_class(thread)        ((thread)->init_class)
#define xnthread_base_class(thread)        ((thread)->base_class)
#define xnthread_sched_class(thread)       ((thread)->sched_class)
#define xnthread_time_slice(thread)        ((thread)->rrperiod)
#define xnthread_archtcb(thread)           (&((thread)->tcb))
#define xnthread_asr_level(thread)         ((thread)->asrlevel)
#define xnthread_pending_signals(thread)  ((thread)->signals)
#define xnthread_timeout(thread)           xntimer_get_timeout(&(thread)->rtimer)
#define xnthread_stack_size(thread)        xnarch_stack_size(xnthread_archtcb(thread))
#define xnthread_stack_base(thread)        xnarch_stack_base(xnthread_archtcb(thread))
#define xnthread_stack_end(thread)         xnarch_stack_end(xnthread_archtcb(thread))
#define xnthread_handle(thread)            ((thread)->registry.handle)
#define xnthread_signaled_p(thread)        ((thread)->signals != 0)
#define xnthread_host_task(thread)         (xnthread_archtcb(thread)->core.host_task)
#define xnthread_host_pid(thread)	   (xnthread_test_state((thread),XNROOT) ? 0 : \
					    xnthread_archtcb(thread)->core.host_task->pid)
#define xnthread_affinity(thread)          ((thread)->affinity)
#define xnthread_affine_p(thread, cpu)     cpu_isset(cpu, (thread)->affinity)
#define xnthread_get_exectime(thread)      xnstat_exectime_get_total(&(thread)->stat.account)
#define xnthread_get_lastswitch(thread)    xnstat_exectime_get_last_switch((thread)->sched)
#define xnthread_inc_rescnt(thread)        ({ (thread)->hrescnt++; })
#define xnthread_dec_rescnt(thread)        ({ --(thread)->hrescnt; })
#define xnthread_get_rescnt(thread)        ((thread)->hrescnt)
#define xnthread_private(thread)           ((thread)->privdata)
#define xnthread_personality(thread)       ((thread)->personality)

#define xnthread_for_each_claimed(__pos, __thread)		\
	list_for_each_entry(__pos, &(__thread)->claimq, link)

#define xnthread_for_each_claimed_safe(__pos, __tmp, __thread)	\
	list_for_each_entry_safe(__pos, __tmp, &(__thread)->claimq, link)

#define xnthread_run_handler(__t, __h)					\
	do {								\
		struct xnpersonality *__p__ = (__t)->personality;	\
		do {							\
			if ((__p__)->ops.__h == NULL)			\
				break;					\
			__p__ = (__p__)->ops.__h(__t);			\
		} while (__p__);					\
	} while (0)
	
static inline
struct xnthread_wait_context *xnthread_get_wait_context(struct xnthread *thread)
{
	return thread->wcontext;
}

static inline
int xnthread_register(struct xnthread *thread, const char *name)
{
	return xnregistry_enter(name, thread, &xnthread_handle(thread), NULL);
}

static inline
struct xnthread *xnthread_lookup(xnhandle_t threadh)
{
	struct xnthread *thread = (struct xnthread *)xnregistry_lookup(threadh);
	return (thread && xnthread_handle(thread) == threadh) ? thread : NULL;
}

static inline void xnthread_sync_window(struct xnthread *thread)
{
	if (thread->u_window)
		thread->u_window->state = thread->state;
}

static inline
void xnthread_clear_sync_window(struct xnthread *thread, int bits)
{
	if (thread->u_window)
		thread->u_window->state = thread->state & ~bits;
}

static inline
void xnthread_set_sync_window(struct xnthread *thread, int bits)
{
	if (thread->u_window)
		thread->u_window->state = thread->state | bits;
}

/*
 * XXX: Mutual dependency issue with synch.h, we have to define
 * xnsynch_release() here.
 */
static inline struct xnthread *
xnsynch_release(struct xnsynch *synch, struct xnthread *thread)
{
	atomic_long_t *lockp;
	xnhandle_t threadh;

	XENO_BUGON(NUCLEUS, !testbits(synch->status, XNSYNCH_OWNER));

	trace_mark(xn_nucleus, synch_release, "synch %p", synch);

	if (unlikely(xnthread_test_state(thread, XNWEAK)))
		__xnsynch_fixup_rescnt(thread);

	lockp = xnsynch_fastlock(synch);
	threadh = xnthread_handle(thread);
	if (likely(xnsynch_fast_release(lockp, threadh)))
		return NULL;

	return __xnsynch_transfer_ownership(synch, thread);
}

int xnthread_init(struct xnthread *thread,
		  const struct xnthread_init_attr *attr,
		  struct xnsched *sched,
		  struct xnsched_class *sched_class,
		  const union xnsched_policy_param *sched_param);

void xnthread_init_shadow_tcb(struct xnthread *thread,
			      struct task_struct *task);

void xnthread_init_root_tcb(struct xnthread *thread);

void xnthread_cleanup(struct xnthread *thread);

char *xnthread_format_status(xnflags_t status, char *buf, int size);

xnticks_t xnthread_get_timeout(struct xnthread *thread, xnticks_t tsc_ns);

xnticks_t xnthread_get_period(struct xnthread *thread);

void xnthread_prepare_wait(struct xnthread_wait_context *wc);

void xnthread_finish_wait(struct xnthread_wait_context *wc,
			  void (*cleanup)(struct xnthread_wait_context *wc));

static inline int normalize_priority(int prio)
{
	return prio < MAX_RT_PRIO ? prio : MAX_RT_PRIO - 1;
}

#endif /* __KERNEL__ */

#endif /* !_COBALT_KERNEL_THREAD_H */

/*
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
 */

#ifndef _XENO_NUCLEUS_THREAD_H
#define _XENO_NUCLEUS_THREAD_H

#include <nucleus/timer.h>

/* Status flags */
#define XNSUSP    0x00000001	/* Suspended */
#define XNPEND    0x00000002	/* Sleep-wait for a resource */
#define XNDELAY   0x00000004	/* Delayed */
#define XNREADY   0x00000008	/* Linked to the ready queue */
#define XNDORMANT 0x00000010	/* Not started yet or killed */
#define XNZOMBIE  0x00000020	/* Zombie thread in deletion process */
#define XNRESTART 0x00000040	/* Restarting thread */
#define XNSTARTED 0x00000080	/* Could be restarted */
#define XNRELAX   0x00000100	/* Relaxed shadow thread (blocking bit) */
#define XNHELD    0x00000200	/* Held thread from suspended partition */

#define XNTIMEO   0x00000400	/* Woken up due to a timeout condition */
#define XNRMID    0x00000800	/* Pending on a removed resource */
#define XNBREAK   0x00001000	/* Forcibly awaken from a wait state */
#define XNKICKED  0x00002000	/* Kicked upon Linux signal (shadow only) */
#define XNBOOST   0x00004000	/* Undergoes regular PIP boost */
#define XNDEBUG   0x00008000	/* Hit debugger breakpoint (shadow only) */

/* Mode flags. */
#define XNLOCK    0x00010000	/* Not preemptible */
#define XNRRB     0x00020000	/* Undergoes a round-robin scheduling */
#define XNASDI    0x00040000	/* ASR are disabled */
#define XNSHIELD  0x00080000	/* IRQ shield is enabled (shadow only) */
#define XNTRAPSW  0x00100000	/* Trap execution mode switches */
#define XNRPIOFF  0x00200000	/* Stop priority coupling (shadow only) */

#define XNFPU     0x00400000	/* Thread uses FPU */
#define XNSHADOW  0x00800000	/* Shadow thread */
#define XNROOT    0x01000000	/* Root thread (i.e. Linux/IDLE) */
#define XNINVPS   0x02000000	/* Using inverted priority scale */

/*
  Must follow the declaration order of the above bits. Status symbols
  are defined as follows:
  'S' -> Forcibly suspended.
  'w'/'W' -> Waiting for a resource, with or without timeout.
  'D' -> Delayed (without any other wait condition).
  'R' -> Runnable.
  'U' -> Unstarted or dormant.
  'X' -> Relaxed shadow.
  'H' -> Held thread.
  'b' -> Priority boost undergoing.
  'T' -> Ptraced and stopped.
  'l' -> Locks scheduler.
  'r' -> Undergoes round-robin .
  's' -> Interrupt shield enabled.
  't' -> Mode switches trapped.
  'o' -> Priority coupling off.
  'f' -> FPU enabled (for kernel threads).
*/
#define XNTHREAD_SLABEL_INIT { \
  'S', 'W', 'D', 'R', 'U', \
  '.', '.', '.', 'X', 'H', \
  '.', '.', '.', '.', 'b', 'T', \
  'l', 'r', '.', 's', 't', 'o', \
  'f', '.', '.', '.'		\
}

#define XNTHREAD_BLOCK_BITS   (XNSUSP|XNPEND|XNDELAY|XNDORMANT|XNRELAX|XNHELD)
#define XNTHREAD_MODE_BITS    (XNLOCK|XNRRB|XNASDI|XNSHIELD|XNTRAPSW|XNRPIOFF)

/* These flags are available to the real-time interfaces */
#define XNTHREAD_SPARE0  0x10000000
#define XNTHREAD_SPARE1  0x20000000
#define XNTHREAD_SPARE2  0x40000000
#define XNTHREAD_SPARE3  0x80000000
#define XNTHREAD_SPARES  0xf0000000

#if defined(__KERNEL__) || defined(__XENO_UVM__) || defined(__XENO_SIM__)

#ifdef __XENO_SIM__
/* Pseudo-status (must not conflict with other bits) */
#define XNRUNNING  XNTHREAD_SPARE0
#define XNDELETED  XNTHREAD_SPARE1
#endif /* __XENO_SIM__ */

#define XNTHREAD_INVALID_ASR  ((void (*)(xnsigmask_t))0)

struct xnsched;
struct xnsynch;

typedef void (*xnasr_t)(xnsigmask_t sigs);

typedef struct xnthread {

    xnarchtcb_t tcb;		/* Architecture-dependent block -- Must be first */

    xnflags_t status;		/* Thread status flags */

    struct xnsched *sched;	/* Thread scheduler */

    xnarch_cpumask_t affinity;	/* Processor affinity. */

    int bprio;			/* Base priority (before PIP boost) */

    int cprio;			/* Current priority */

    xnpholder_t rlink;		/* Thread holder in ready queue */

    xnpholder_t plink;		/* Thread holder in synchronization queue(s) */

    xnholder_t glink;		/* Thread holder in global queue */

/* We don't want side-effects on laddr here! */
#define link2thread(laddr,link) \
((xnthread_t *)(((char *)laddr) - (int)(&((xnthread_t *)0)->link)))

    xnpqueue_t claimq;		/* Owned resources claimed by others (PIP) */

    struct xnsynch *wchan;	/* Resource the thread pends on */

    xntimer_t rtimer;		/* Resource timer */

    xntimer_t ptimer;		/* Periodic timer */

    xnticks_t pexpect;		/* Date of next periodic release point (raw ticks). */

    xnsigmask_t signals;	/* Pending core signals */

    xnticks_t rrperiod;		/* Allotted round-robin period (ticks) */

    xnticks_t rrcredit;		/* Remaining round-robin time credit (ticks) */

#ifdef CONFIG_XENO_OPT_STATS
    struct {
	unsigned long ssw;	/* Primary -> secondary mode switch count */
	unsigned long csw;	/* Context switches (includes
				   secondary -> primary switches) */
	unsigned long pf;	/* Number of page faults */
       xnticks_t exec_time;    /* Accumulated execution time (tsc) */
       xnticks_t exec_start;   /* Start of execution time accumulation (tsc) */
    } stat;
#endif /* CONFIG_XENO_OPT_STATS */

    int errcode;		/* Local errno */

    xnasr_t asr;		/* Asynchronous service routine */

    xnflags_t asrmode;		/* Thread's mode for ASR */

    int asrimask;		/* Thread's interrupt mask for ASR */

    unsigned asrlevel;		/* ASR execution level (ASRs are reentrant) */

    int imask;			/* Initial interrupt mask */

    int imode;			/* Initial mode */

    int iprio;			/* Initial priority */

#ifdef CONFIG_XENO_OPT_REGISTRY
    struct {
	xnhandle_t handle;	/* Handle in registry */
	const char *waitkey;	/* Pended key */
    } registry;
#endif /* CONFIG_XENO_OPT_REGISTRY */

    unsigned magic;		/* Skin magic. */

    char name[XNOBJECT_NAME_LEN]; /* Symbolic name of thread */

    xnticks_t stime;		/* Start time */

    void (*entry)(void *cookie); /* Thread entry routine */

    void *cookie;		/* Cookie to pass to the entry routine */

    XNARCH_DECL_DISPLAY_CONTEXT();

} xnthread_t;

#define XNHOOK_THREAD_START  1
#define XNHOOK_THREAD_SWITCH 2
#define XNHOOK_THREAD_DELETE 3

typedef struct xnhook {

    xnholder_t link;

#define link2hook(laddr) \
((xnhook_t *)(((char *)laddr) - (int)(&((xnhook_t *)0)->link)))

    void (*routine)(xnthread_t *thread);

} xnhook_t;

#define xnthread_name(thread)              ((thread)->name)
#define xnthread_clear_name(thread)        do { *(thread)->name = 0; } while(0)
#define xnthread_sched(thread)             ((thread)->sched)
#define xnthread_start_time(thread)        ((thread)->stime)
#define xnthread_status_flags(thread)      ((thread)->status)
#define xnthread_test_flags(thread,flags)  testbits((thread)->status,flags)
#define xnthread_set_flags(thread,flags)   __setbits((thread)->status,flags)
#define xnthread_clear_flags(thread,flags) __clrbits((thread)->status,flags)
#define xnthread_initial_priority(thread)  ((thread)->iprio)
#define xnthread_base_priority(thread)     ((thread)->bprio)
#define xnthread_current_priority(thread)  ((thread)->cprio)
#define xnthread_time_slice(thread)        ((thread)->rrperiod)
#define xnthread_time_credit(thread)       ((thread)->rrcredit)
#define xnthread_archtcb(thread)           (&((thread)->tcb))
#define xnthread_asr_level(thread)         ((thread)->asrlevel)
#define xnthread_pending_signals(thread)   ((thread)->signals)
#define xnthread_timeout(thread)           xntimer_get_timeout(&(thread)->rtimer)
#define xnthread_stack_size(thread)        xnarch_stack_size(xnthread_archtcb(thread))
#define xnthread_handle(thread)            ((thread)->registry.handle)
#define xnthread_set_magic(thread,m)       do { (thread)->magic = (m); } while(0)
#define xnthread_get_magic(thread)         ((thread)->magic)
#define xnthread_signaled_p(thread)        ((thread)->signals != 0)
#define xnthread_user_task(thread)         xnarch_user_task(xnthread_archtcb(thread))
#define xnthread_user_pid(thread) \
    (testbits((thread)->status,XNROOT) || !xnthread_user_task(thread) ? \
    0 : xnarch_user_pid(xnthread_archtcb(thread)))

#ifdef CONFIG_XENO_OPT_STATS
#define xnthread_inc_ssw(thread)     ++(thread)->stat.ssw
#define xnthread_inc_csw(thread)     ++(thread)->stat.csw
#define xnthread_inc_pf(thread)      ++(thread)->stat.pf
#else /* CONFIG_XENO_OPT_STATS */
#define xnthread_inc_ssw(thread)     do { } while(0)
#define xnthread_inc_csw(thread)     do { } while(0)
#define xnthread_inc_pf(thread)      do { } while(0)
#endif /* CONFIG_XENO_OPT_STATS */

#ifdef __cplusplus
extern "C" {
#endif

int xnthread_init(xnthread_t *thread,
		  const char *name,
		  int prio,
		  xnflags_t flags,
		  unsigned stacksize);

void xnthread_cleanup_tcb(xnthread_t *thread);

char *xnthread_symbolic_status(xnflags_t status, char *buf, int size);

int *xnthread_get_errno_location(void);

static inline xnticks_t xnthread_get_timeout(xnthread_t *thread, xnticks_t now)
{
    xnticks_t timeout;

    if (!testbits(thread->status,XNDELAY))
	return 0LL;

    timeout = (xntimer_get_date(&thread->rtimer) ? : xntimer_get_date(&thread->ptimer));

    if (timeout <= now)
	return 1;

    return timeout - now;
}

#ifdef __cplusplus
}
#endif

#endif /* __KERNEL__ || __XENO_UVM__ || __XENO_SIM__ */

#endif /* !_XENO_NUCLEUS_THREAD_H */

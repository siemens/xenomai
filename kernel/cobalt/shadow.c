/**
 * Copyright (C) 2001-2013 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) 2001-2013 The Xenomai project <http://www.xenomai.org>
 * Copyright (C) 2006 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>
 *
 * SMP support Copyright (C) 2004 The HYADES project <http://www.hyades-itea.org>
 * RTAI/fusion Copyright (C) 2004 The RTAI project <http://www.rtai.org>
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
 * @defgroup shadow Real-time shadow services.
 *
 * Real-time shadow services.
 *
 *@{
 */
#include <stdarg.h>
#include <linux/unistd.h>
#include <linux/wait.h>
#include <linux/semaphore.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/cred.h>
#include <linux/jhash.h>
#include <linux/file.h>
#include <linux/ptrace.h>
#include <linux/vmalloc.h>
#include <linux/completion.h>
#include <linux/kallsyms.h>
#include <asm/signal.h>
#include <cobalt/kernel/sched.h>
#include <cobalt/kernel/heap.h>
#include <cobalt/kernel/synch.h>
#include <cobalt/kernel/clock.h>
#include <cobalt/kernel/shadow.h>
#include <cobalt/kernel/ppd.h>
#include <cobalt/kernel/trace.h>
#include <cobalt/kernel/stat.h>
#include <cobalt/kernel/ppd.h>
#include <cobalt/kernel/vdso.h>
#include <cobalt/kernel/thread.h>
#include <asm/xenomai/features.h>
#include <asm/xenomai/syscall.h>
#include <asm-generic/xenomai/mayday.h>
#include "debug.h"

#define EVENT_PROPAGATE   0
#define EVENT_STOP        1

static int xn_gid_arg = -1;
module_param_named(xenomai_gid, xn_gid_arg, int, 0644);
MODULE_PARM_DESC(xenomai_gid, "GID of the group with access to Xenomai services");

#define PERSONALITIES_NR  4

struct xnpersonality *personalities[PERSONALITIES_NR];

static int user_muxid = -1;

static DEFINE_SEMAPHORE(registration_mutex);

static void *mayday_page;

static struct xnsynch yield_sync;

static struct list_head *ppd_hash;
#define PPD_HASH_SIZE 13

union xnshadow_ppd_hkey {
	struct mm_struct *mm;
	uint32_t val;
};

/*
 * ppd holder with the same mm collide and are stored contiguously in
 * the same bucket, so that they can all be destroyed with only one
 * hash lookup by ppd_remove_mm.
 */
static unsigned int ppd_lookup_inner(struct list_head **pq,
				     struct xnshadow_ppd **pholder,
				     struct xnshadow_ppd_key *pkey)
{
	union xnshadow_ppd_hkey key = { .mm = pkey->mm };
	struct xnshadow_ppd *ppd = NULL;
	unsigned int bucket;

	bucket = jhash2(&key.val, sizeof(key) / sizeof(uint32_t), 0);
	*pq = &ppd_hash[bucket % PPD_HASH_SIZE];

	if (list_empty(*pq))
		goto out;

	list_for_each_entry(ppd, *pq, link) {
		if (ppd->key.mm == pkey->mm && ppd->key.muxid == pkey->muxid) {
			*pholder = ppd;
			return 1; /* Exact match. */
		}
		/*
		 * Order by increasing mm address. Within the same mm,
		 * order by decreasing muxid.
		 */
		if (ppd->key.mm > pkey->mm ||
		    (ppd->key.mm == pkey->mm && ppd->key.muxid < pkey->muxid))
			/* Not found, return successor for insertion. */
			goto out;
	}

	ppd = NULL;
out:
	*pholder = ppd;

	return 0;
}

static int ppd_insert(struct xnshadow_ppd *holder)
{
	struct xnshadow_ppd *next;
	struct list_head *q;
	unsigned int found;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	found = ppd_lookup_inner(&q, &next, &holder->key);
	if (found) {
		xnlock_put_irqrestore(&nklock, s);
		return -EBUSY;
	}

	if (next)
		list_add_tail(&holder->link, &next->link);
	else
		list_add_tail(&holder->link, q);

	xnlock_put_irqrestore(&nklock, s);

	return 0;
}

/* nklock locked, irqs off. */
static struct xnshadow_ppd *ppd_lookup(unsigned int muxid,
				       struct mm_struct *mm)
{
	struct xnshadow_ppd_key key;
	struct xnshadow_ppd *holder;
	struct list_head *q;
	unsigned int found;

	key.muxid = muxid;
	key.mm = mm;

	found = ppd_lookup_inner(&q, &holder, &key);
	if (!found)
		return NULL;

	return holder;
}

static void ppd_remove(struct xnshadow_ppd *holder)
{
	struct list_head *q;
	unsigned int found;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	found = ppd_lookup_inner(&q, &holder, &holder->key);
	if (found)
		list_del(&holder->link);

	xnlock_put_irqrestore(&nklock, s);
}

static inline void ppd_remove_mm(struct mm_struct *mm,
				 void (*destructor) (struct xnshadow_ppd *))
{
	struct xnshadow_ppd *ppd, *next;
	struct xnshadow_ppd_key key;
	struct list_head *q;
	spl_t s;

	key.muxid = ~0UL; /* seek first muxid for 'mm'. */
	key.mm = mm;
	xnlock_get_irqsave(&nklock, s);
	ppd_lookup_inner(&q, &ppd, &key);

	while (ppd && ppd->key.mm == mm) {
		if (list_is_last(&ppd->link, q))
			next = NULL;
		else
			next = list_next_entry(ppd, link);
		list_del(&ppd->link);
		xnlock_put_irqrestore(&nklock, s);
		/*
		 * Releasing the nklock is safe here, if we assume
		 * that no insertion for the same mm will take place
		 * while we are running ppd_remove_mm().
		 */
		destructor(ppd);
		ppd = next;
		xnlock_get_irqsave(&nklock, s);
	}

	xnlock_put_irqrestore(&nklock, s);
}

static void detach_ppd(struct xnshadow_ppd *ppd)
{
	struct xnpersonality *personality;
	unsigned int muxid;

	muxid = xnshadow_ppd_muxid(ppd);
	personality = personalities[muxid];
	personality->ops.detach_process(ppd);
	if (personality->module)
		module_put(personality->module);
}

static void request_syscall_restart(struct xnthread *thread,
				    struct pt_regs *regs,
				    int sysflags)
{
	int notify = 0;

	if (xnthread_test_info(thread, XNKICKED)) {
		if (__xn_interrupted_p(regs)) {
			__xn_error_return(regs,
					  (sysflags & __xn_exec_norestart) ?
					  -EINTR : -ERESTARTSYS);
			notify = !xnthread_test_state(thread, XNDEBUG);
		}
		xnthread_clear_info(thread, XNKICKED);
	}

	xnthread_test_cancel();

	xnshadow_relax(notify, SIGDEBUG_MIGRATE_SIGNAL);
}

static inline void lock_timers(void)
{
	smp_mb__before_atomic_inc();
	atomic_inc(&nkclklk);
	smp_mb__after_atomic_inc();
}

static inline void unlock_timers(void)
{
	XENO_BUGON(NUCLEUS, atomic_read(&nkclklk) == 0);
	smp_mb__before_atomic_dec();
	atomic_dec(&nkclklk);
	smp_mb__after_atomic_dec();
}

static int enter_personality(struct xnpersonality *personality)
{
	if (personality->module && !try_module_get(personality->module))
		return -ENOSYS;

	atomic_inc(&personality->refcnt);

	return 0;
}

static void leave_personality(struct xnpersonality *personality)
{
	atomic_dec(&personality->refcnt);
	if (personality->module)
		module_put(personality->module);
}

struct lostage_wakeup {
	struct ipipe_work_header work; /* Must be first. */
	struct task_struct *task;
};

static void lostage_task_wakeup(struct ipipe_work_header *work)
{
	struct lostage_wakeup *rq;
	struct task_struct *p;

	rq = container_of(work, struct lostage_wakeup, work);
	p = rq->task;

	trace_mark(xn_nucleus, lostage_wakeup, "comm %s pid %d",
		   p->comm, p->pid);

	wake_up_process(p);
}

static void post_wakeup(struct task_struct *p)
{
	struct lostage_wakeup wakework = {
		.work = {
			.size = sizeof(wakework),
			.handler = lostage_task_wakeup,
		},
		.task = p,
	};

	ipipe_post_work_root(&wakework, work);
}

struct lostage_signal {
	struct ipipe_work_header work; /* Must be first. */
	struct task_struct *task;
	int signo, sigval;
};

static inline void do_kthread_signal(struct task_struct *p,
				     struct xnthread *thread,
				     struct lostage_signal *rq)
{
	printk(XENO_WARN
	       "kernel shadow %s received unhandled signal %d (action=0x%x)\n",
	       thread->name, rq->signo, rq->sigval);
}

static void lostage_task_signal(struct ipipe_work_header *work)
{
	struct lostage_signal *rq;
	struct xnthread *thread;
	struct task_struct *p;
	siginfo_t si;
	int signo;

	rq = container_of(work, struct lostage_signal, work);
	p = rq->task;

	thread = xnshadow_thread(p);
	if (thread && !xnthread_test_state(thread, XNUSER)) {
		do_kthread_signal(p, thread, rq);
		return;
	}

	signo = rq->signo;

	trace_mark(xn_nucleus, lostage_signal, "comm %s pid %d sig %d",
		   p->comm, p->pid, signo);

	if (signo == SIGSHADOW || signo == SIGDEBUG) {
		memset(&si, '\0', sizeof(si));
		si.si_signo = signo;
		si.si_code = SI_QUEUE;
		si.si_int = rq->sigval;
		send_sig_info(signo, &si, p);
	} else
		send_sig(signo, p, 1);
}

#ifdef CONFIG_SMP

static int handle_setaffinity_event(struct ipipe_cpu_migration_data *d)
{
	struct task_struct *p = d->task;
	struct xnthread *thread;
	struct xnsched *sched;
	spl_t s;

	thread = xnshadow_thread(p);
	if (thread == NULL)
		return EVENT_PROPAGATE;

	/*
	 * The CPU affinity mask is always controlled from secondary
	 * mode, therefore we progagate any change to the real-time
	 * affinity mask accordingly.
	 */
	xnlock_get_irqsave(&nklock, s);
	cpus_and(thread->affinity, p->cpus_allowed, nkaffinity);
	xnlock_put_irqrestore(&nklock, s);

	/*
	 * If kernel and real-time CPU affinity sets are disjoints,
	 * there might be problems ahead for this thread next time it
	 * moves back to primary mode, if it ends up switching to an
	 * unsupported CPU.
	 *
	 * Otherwise, check_affinity() will extend the CPU affinity if
	 * possible, fixing up the thread's affinity mask. This means
	 * that a thread might be allowed to run with a broken
	 * (i.e. fully cleared) affinity mask until it leaves primary
	 * mode then switches back to it, in SMP configurations.
	 */
	if (cpus_empty(thread->affinity))
		printk(XENO_WARN "thread %s[%d] changed CPU affinity inconsistently\n",
		       thread->name, xnthread_host_pid(thread));
	else {
		xnlock_get_irqsave(&nklock, s);
		/*
		 * Threads running in primary mode may NOT be forcibly
		 * migrated by the regular kernel to another CPU. Such
		 * migration would have to wait until the thread
		 * switches back from secondary mode at some point
		 * later, or issues a call to xnthread_migrate().
		 */
		if (!xnthread_test_state(thread, XNMIGRATE) &&
		    xnthread_test_state(thread, XNTHREAD_BLOCK_BITS)) {
			sched = xnsched_struct(d->dest_cpu);
			xnthread_migrate_passive(thread, sched);
		}
		xnlock_put_irqrestore(&nklock, s);
	}

	return EVENT_PROPAGATE;
}

static inline void check_affinity(struct task_struct *p) /* nklocked, IRQs off */
{
	struct xnthread *thread = xnshadow_thread(p);
	struct xnsched *sched;
	int cpu = task_cpu(p);

	/*
	 * If the task moved to another CPU while in secondary mode,
	 * migrate the companion Xenomai shadow to reflect the new
	 * situation.
	 *
	 * In the weirdest case, the thread is about to switch to
	 * primary mode on a CPU Xenomai shall not use. This is
	 * hopeless, whine and kill that thread asap.
	 */
	if (!cpu_isset(cpu, xnsched_realtime_cpus)) {
		printk(XENO_WARN "thread %s[%d] switched to non-rt CPU, aborted.\n",
		       thread->name, xnthread_host_pid(thread));
		/*
		 * Can't call xnthread_cancel() from a migration
		 * point, that would break. Since we are on the wakeup
		 * path to hardening, just raise XNCANCELD to catch it
		 * in xnshadow_harden().
		 */
		xnthread_set_info(thread, XNCANCELD);
		return;
	}

	sched = xnsched_struct(cpu);
	if (sched == thread->sched)
		return;

	/*
	 * The current thread moved to a supported real-time CPU,
	 * which is not part of its original affinity mask
	 * though. Assume user wants to extend this mask.
	 */
	if (!cpu_isset(cpu, thread->affinity))
		cpu_set(cpu, thread->affinity);

	xnthread_migrate_passive(thread, sched);
}

#else /* !CONFIG_SMP */

struct ipipe_cpu_migration_data;

static int handle_setaffinity_event(struct ipipe_cpu_migration_data *d)
{
	return EVENT_PROPAGATE;
}

static inline void check_affinity(struct task_struct *p) { }

#endif /* CONFIG_SMP */

/*!
 * @internal
 * \fn int xnshadow_harden(void);
 * \brief Migrate a Linux task to the Xenomai domain.
 *
 * This service causes the transition of "current" from the Linux
 * domain to Xenomai. The shadow will resume in the Xenomai domain as
 * returning from schedule().
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - User-space thread operating in secondary (i.e. relaxed) mode.
 *
 * Rescheduling: always.
 */
void ipipe_migration_hook(struct task_struct *p) /* hw IRQs off */
{
	struct xnthread *thread = xnshadow_thread(p);

	/*
	 * We fire the handler before the thread is migrated, so that
	 * thread->sched does not change between paired invocations of
	 * relax_thread/harden_thread handlers.
	 */
	xnlock_get(&nklock);
	xnthread_run_handler(thread, harden_thread);
	check_affinity(p);
	xnthread_resume(thread, XNRELAX);
	xnlock_put(&nklock);

	xnsched_run();
}

int xnshadow_harden(void)
{
	struct task_struct *p = current;
	struct xnthread *thread;
	struct xnsched *sched;
	int ret;

	thread = xnshadow_current();
	if (thread == NULL)
		return -EPERM;

	if (signal_pending(p))
		return -ERESTARTSYS;

	trace_mark(xn_nucleus, shadow_gohard,
		   "thread %p name %s comm %s",
		   thread, xnthread_name(thread), p->comm);

	xnthread_clear_sync_window(thread, XNRELAX);

	ret = __ipipe_migrate_head();
	if (ret) {
		xnthread_set_sync_window(thread, XNRELAX);
		return ret;
	}

	/* "current" is now running into the Xenomai domain. */
	sched = xnsched_finish_unlocked_switch(thread->sched);
	xnthread_switch_fpu(sched);

	xnlock_clear_irqon(&nklock);
	xnsched_resched_after_unlocked_switch();
	xnthread_test_cancel();

	trace_mark(xn_nucleus, shadow_hardened, "thread %p name %s",
		   thread, xnthread_name(thread));

	/*
	 * Recheck pending signals once again. As we block task
	 * wakeups during the migration and handle_sigwake_event()
	 * ignores signals until XNRELAX is cleared, any signal
	 * between entering TASK_HARDENING and starting the migration
	 * is just silently queued up to here.
	 */
	if (signal_pending(p)) {
		xnshadow_relax(!xnthread_test_state(thread, XNDEBUG),
			       SIGDEBUG_MIGRATE_SIGNAL);
		return -ERESTARTSYS;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(xnshadow_harden);

/*!
 * @internal
 * \fn void xnshadow_relax(int notify, int reason);
 * \brief Switch a shadow thread back to the Linux domain.
 *
 * This service yields the control of the running shadow back to
 * Linux. This is obtained by suspending the shadow and scheduling a
 * wake up call for the mated user task inside the Linux domain. The
 * Linux task will resume on return from xnthread_suspend() on behalf
 * of the root thread.
 *
 * @param notify A boolean flag indicating whether threads monitored
 * from secondary mode switches should be sent a SIGDEBUG signal. For
 * instance, some internal operations like task exit should not
 * trigger such signal.
 *
 * @param reason The reason to report along with the SIGDEBUG signal.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - User-space thread operating in primary (i.e. harden) mode.
 *
 * Rescheduling: always.
 *
 * @note "current" is valid here since the shadow runs with the
 * properties of the Linux task.
 */
void xnshadow_relax(int notify, int reason)
{
	struct xnthread *thread = xnsched_current_thread();
	struct task_struct *p = current;
	int cpu __maybe_unused;
	siginfo_t si;

	XENO_BUGON(NUCLEUS, xnthread_test_state(thread, XNROOT));

	/*
	 * Enqueue the request to move the running shadow from the Xenomai
	 * domain to the Linux domain.  This will cause the Linux task
	 * to resume using the register state of the shadow thread.
	 */
	trace_mark(xn_nucleus, shadow_gorelax, "thread %p thread_name %s",
		  thread, xnthread_name(thread));

	/*
	 * If you intend to change the following interrupt-free
	 * sequence, /first/ make sure to check the special handling
	 * of XNRELAX in xnthread_suspend() when switching out the
	 * current thread, not to break basic assumptions we make
	 * there.
	 *
	 * We disable interrupts during the migration sequence, but
	 * xnthread_suspend() has an interrupts-on section built in.
	 */
	splmax();
	post_wakeup(p);
	/*
	 * Grab the nklock to synchronize the Linux task state
	 * manipulation with handle_sigwake_event. This lock will be
	 * dropped by xnthread_suspend().
	 */
	xnlock_get(&nklock);
	set_task_state(p, p->state & ~TASK_NOWAKEUP);
	xnthread_run_handler(thread, relax_thread);
	xnthread_suspend(thread, XNRELAX, XN_INFINITE, XN_RELATIVE, NULL);
	splnone();

	if (XENO_DEBUG(NUCLEUS) && !ipipe_root_p)
		xnsys_fatal("xnshadow_relax() failed for thread %s[%d]",
			    thread->name, xnthread_host_pid(thread));

	__ipipe_reenter_root();

	/* Account for secondary mode switch. */
	xnstat_counter_inc(&thread->stat.ssw);

	if (xnthread_test_state(thread, XNUSER) && notify) {
		xndebug_notify_relax(thread, reason);
		if (xnthread_test_state(thread, XNTRAPSW)) {
			/* Help debugging spurious relaxes. */
			memset(&si, 0, sizeof(si));
			si.si_signo = SIGDEBUG;
			si.si_code = SI_QUEUE;
			si.si_int = reason;
			send_sig_info(SIGDEBUG, &si, p);
		}
		xnsynch_detect_claimed_relax(thread);
	}

	/*
	 * "current" is now running into the Linux domain on behalf of
	 * the root thread.
	 */
	xnthread_sync_window(thread);

#ifdef CONFIG_SMP
	if (xnthread_test_info(thread, XNMOVED)) {
		xnthread_clear_info(thread, XNMOVED);
		cpu = xnsched_cpu(thread->sched);
		set_cpus_allowed(p, cpumask_of_cpu(cpu));
	}
#endif

	trace_mark(xn_nucleus, shadow_relaxed,
		  "thread %p thread_name %s comm %s",
		  thread, xnthread_name(thread), p->comm);
}
EXPORT_SYMBOL_GPL(xnshadow_relax);

static int force_wakeup(struct xnthread *thread) /* nklock locked, irqs off */
{
	int ret = 0;

	if (xnthread_test_info(thread, XNKICKED))
		return 1;

	if (xnthread_unblock(thread)) {
		xnthread_set_info(thread, XNKICKED);
		ret = 1;
	}

	/*
	 * CAUTION: we must NOT raise XNBREAK when clearing a forcible
	 * block state, such as XNSUSP, XNHELD. The caller of
	 * xnthread_suspend() we unblock shall proceed as for a normal
	 * return, until it traverses a cancellation point if
	 * XNCANCELD was raised earlier, or calls xnthread_suspend()
	 * which will detect XNKICKED and act accordingly.
	 *
	 * Rationale: callers of xnthread_suspend() may assume that
	 * receiving XNBREAK means that the process that motivated the
	 * blocking did not go to completion. E.g. the wait context
	 * (see. xnthread_prepare_wait()) was NOT posted before
	 * xnsynch_sleep_on() returned, leaving no useful data there.
	 * Therefore, in case only XNSUSP remains set for the thread
	 * on entry to force_wakeup(), after XNPEND was lifted earlier
	 * when the wait went to successful completion (i.e. no
	 * timeout), then we want the kicked thread to know that it
	 * did receive the requested resource, not finding XNBREAK in
	 * its state word.
	 *
	 * Callers of xnthread_suspend() may inquire for XNKICKED to
	 * detect forcible unblocks from XNSUSP, XNHELD, if they
	 * should act upon this case specifically.
	 */
	if (xnthread_test_state(thread, XNSUSP|XNHELD)) {
		xnthread_resume(thread, XNSUSP|XNHELD);
		xnthread_set_info(thread, XNKICKED);
	}

	/*
	 * Tricky cases:
	 *
	 * - a thread which was ready on entry wasn't actually
	 * running, but nevertheless waits for the CPU in primary
	 * mode, so we have to make sure that it will be notified of
	 * the pending break condition as soon as it enters
	 * xnthread_suspend() from a blocking Xenomai syscall.
	 *
	 * - a ready/readied thread on exit may be prevented from
	 * running by the scheduling policy module it belongs
	 * to. Typically, policies enforcing a runtime budget do not
	 * block threads with no budget, but rather keep them out of
	 * their runnable queue, so that ->sched_pick() won't elect
	 * them. We tell the policy handler about the fact that we do
	 * want such thread to run until it relaxes, whatever this
	 * means internally for the implementation.
	 */
	if (xnthread_test_state(thread, XNREADY))
		xnsched_kick(thread);

	return ret;
}

void __xnshadow_kick(struct xnthread *thread) /* nklock locked, irqs off */
{
	struct task_struct *p = xnthread_host_task(thread);

	/* Thread is already relaxed -- nop. */
	if (xnthread_test_state(thread, XNRELAX))
		return;

	/*
	 * First, try to kick the thread out of any blocking syscall
	 * Xenomai-wise. If that succeeds, then the thread will relax
	 * on its return path to user-space.
	 */
	if (force_wakeup(thread))
		return;

	/*
	 * If that did not work out because the thread was not blocked
	 * (i.e. XNPEND/XNDELAY) in a syscall, then force a mayday
	 * trap. Note that we don't want to send that thread any linux
	 * signal, we only want to force it to switch to secondary
	 * mode asap.
	 *
	 * It could happen that a thread is relaxed on a syscall
	 * return path after it was resumed from self-suspension
	 * (e.g. XNSUSP) then also forced to run a mayday trap right
	 * after: this is still correct, at worst we would get a
	 * useless mayday syscall leading to a no-op, no big deal.
	 */
	xnthread_set_info(thread, XNKICKED);

	/*
	 * We may send mayday signals to userland threads only.
	 * However, no need to run a mayday trap if the current thread
	 * kicks itself out of primary mode: it will relax on its way
	 * back to userland via the current syscall
	 * epilogue. Otherwise, we want that thread to enter the
	 * mayday trap asap, to call us back for relaxing.
	 */
	if (thread != xnsched_current_thread() &&
	    xnthread_test_state(thread, XNUSER))
		xnarch_call_mayday(p);
}

void xnshadow_kick(struct xnthread *thread)
{
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	__xnshadow_kick(thread);
	xnlock_put_irqrestore(&nklock, s);
}
EXPORT_SYMBOL_GPL(xnshadow_kick);

void __xnshadow_demote(struct xnthread *thread) /* nklock locked, irqs off */
{
	struct xnsched_class *sched_class;
	union xnsched_policy_param param;

	/*
	 * First we kick the thread out of primary mode, and have it
	 * resume execution immediately over the regular linux
	 * context.
	 */
	__xnshadow_kick(thread);

	/*
	 * Then we demote it, turning that thread into a non real-time
	 * Xenomai shadow, which still has access to Xenomai
	 * resources, but won't compete for real-time scheduling
	 * anymore. In effect, moving the thread to a weak scheduling
	 * class/priority will prevent it from sticking back to
	 * primary mode.
	 */
#ifdef CONFIG_XENO_OPT_SCHED_WEAK
	param.weak.prio = 0;
	sched_class = &xnsched_class_weak;
#else
	param.rt.prio = 0;
	sched_class = &xnsched_class_rt;
#endif
	__xnthread_set_schedparam(thread, sched_class, &param);
}

void xnshadow_demote(struct xnthread *thread)
{
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	__xnshadow_demote(thread);
	xnlock_put_irqrestore(&nklock, s);
}
EXPORT_SYMBOL_GPL(xnshadow_demote);

static inline void init_threadinfo(struct xnthread *thread)
{
	struct ipipe_threadinfo *p;

	p = ipipe_current_threadinfo();
	p->thread = thread;
	p->mm = current->mm;
}

static inline void destroy_threadinfo(void)
{
	struct ipipe_threadinfo *p = ipipe_current_threadinfo();
	p->thread = NULL;
	p->mm = NULL;
}

static void pin_to_initial_cpu(struct xnthread *thread)
{
	struct task_struct *p = current;
	struct xnsched *sched;
	int cpu;
	spl_t s;

	/*
	 * @thread is the Xenomai extension of the current kernel
	 * task. If the current CPU is part of the affinity mask of
	 * this thread, pin the latter on this CPU. Otherwise pin it
	 * to the first CPU of that mask.
	 */
	cpu = task_cpu(p);
	if (!cpu_isset(cpu, thread->affinity))
		cpu = first_cpu(thread->affinity);

	set_cpus_allowed(p, cpumask_of_cpu(cpu));
	/*
	 * @thread is still unstarted Xenomai-wise, we are precisely
	 * in the process of mapping the current kernel task to
	 * it. Therefore xnthread_migrate_passive() is the right way
	 * to pin it on a real-time CPU.
	 */
	xnlock_get_irqsave(&nklock, s);
	sched = xnsched_struct(cpu);
	xnthread_migrate_passive(thread, sched);
	xnlock_put_irqrestore(&nklock, s);
}

/**
 * @fn int xnshadow_map_user(struct xnthread *thread, unsigned long __user *u_window_offset)
 * @internal
 * @brief Create a shadow thread context over a user task.
 *
 * This call maps a nucleus thread to the "current" Linux task running
 * in userland.  The priority and scheduling class of the underlying
 * Linux task are not affected; it is assumed that the interface
 * library did set them appropriately before issuing the shadow
 * mapping request.
 *
 * @param thread The descriptor address of the new shadow thread to be
 * mapped to "current". This descriptor must have been previously
 * initialized by a call to xnthread_init().
 *
 * @param u_window_offset will receive the offset of the per-thread
 * "u_window" structure in the process shared heap associated to @a
 * thread. This structure reflects thread state information visible
 * from userland through a shared memory window.
 *
 * @return 0 is returned on success. Otherwise:
 *
 * - -ERESTARTSYS is returned if the current Linux task has received a
 * signal, thus preventing the final migration to the Xenomai domain
 * (i.e. in order to process the signal in the Linux domain). This
 * error should not be considered as fatal.
 *
 * - -EINVAL is returned if the thread control block does not bear the
 * XNUSER bit.
 *
 * - -EBUSY is returned if either the current Linux task or the
 * associated shadow thread is already involved in a shadow mapping.
 *
 * Calling context: This service may be called on behalf of a regular
 * user-space process.
 *
 * Rescheduling: always.
 *
 */
int xnshadow_map_user(struct xnthread *thread,
		      unsigned long __user *u_window_offset)
{
	struct xnpersonality *personality = thread->personality;
	struct xnthread_user_window *u_window;
	struct task_struct *p = current;
	struct xnthread_start_attr attr;
	struct xnsys_ppd *sys_ppd;
	struct xnheap *sem_heap;
	spl_t s;
	int ret;

	if (!xnthread_test_state(thread, XNUSER))
		return -EINVAL;

	if (xnshadow_current() || xnthread_test_state(thread, XNMAPPED))
		return -EBUSY;

	if (!access_wok(u_window_offset, sizeof(*u_window_offset)))
		return -EFAULT;

	ret = enter_personality(personality);
	if (ret)
		return ret;

#ifdef CONFIG_MMU
	if ((p->mm->def_flags & VM_LOCKED) == 0) {
		siginfo_t si;

		memset(&si, 0, sizeof(si));
		si.si_signo = SIGDEBUG;
		si.si_code = SI_QUEUE;
		si.si_int = SIGDEBUG_NOMLOCK;
		send_sig_info(SIGDEBUG, &si, p);
	} else {
		ret = __ipipe_disable_ondemand_mappings(p);
		if (ret) {
			leave_personality(personality);
			return ret;
		}
	}
#endif /* CONFIG_MMU */

	xnlock_get_irqsave(&nklock, s);
	sys_ppd = xnsys_ppd_get(0);
	xnlock_put_irqrestore(&nklock, s);

	sem_heap = &sys_ppd->sem_heap;
	u_window = xnheap_alloc(sem_heap, sizeof(*u_window));
	if (u_window == NULL) {
		leave_personality(personality);
		return -ENOMEM;
	}
	thread->u_window = u_window;
	__xn_put_user(xnheap_mapped_offset(sem_heap, u_window), u_window_offset);
	pin_to_initial_cpu(thread);

	trace_mark(xn_nucleus, shadow_map_user,
		   "thread %p thread_name %s pid %d priority %d",
		   thread, xnthread_name(thread), current->pid,
		   xnthread_base_priority(thread));

	/*
	 * CAUTION: we enable the pipeline notifier only when our
	 * shadow TCB is consistent, so that we won't trigger false
	 * positive in debug code from handle_schedule_event() and
	 * friends.
	 */
	xnthread_init_shadow_tcb(thread, current);
	xnthread_suspend(thread, XNRELAX, XN_INFINITE, XN_RELATIVE, NULL);
	init_threadinfo(thread);
	xnthread_set_state(thread, XNMAPPED);
	xndebug_shadow_init(thread);
	atomic_inc(&sys_ppd->refcnt);
	/*
	 * ->map_thread() handler is invoked after the TCB is fully
	 * built, and when we know for sure that current will go
	 * through our task-exit handler, because it has a shadow
	 * extension and I-pipe notifications will soon be enabled for
	 * it.
	 */
	xnthread_run_handler(thread, map_thread);
	ipipe_enable_notifier(current);

	attr.mode = 0;
	attr.entry = NULL;
	attr.cookie = NULL;
	ret = xnthread_start(thread, &attr);
	if (ret)
		return ret;

	xnthread_sync_window(thread);

	ret = xnshadow_harden();

	xntrace_pid(xnthread_host_pid(thread),
		    xnthread_current_priority(thread));

	return ret;
}
EXPORT_SYMBOL_GPL(xnshadow_map_user);

struct parent_wakeup_request {
	struct ipipe_work_header work; /* Must be first. */
	struct completion *done;
};

static void do_parent_wakeup(struct ipipe_work_header *work)
{
	struct parent_wakeup_request *rq;

	rq = container_of(work, struct parent_wakeup_request, work);
	complete(rq->done);
}

static inline void wakeup_parent(struct completion *done)
{
	struct parent_wakeup_request wakework = {
		.work = {
			.size = sizeof(wakework),
			.handler = do_parent_wakeup,
		},
		.done = done,
	};

	ipipe_post_work_root(&wakework, work);
}

/**
 * @fn int xnshadow_map_kernel(struct xnthread *thread, struct completion *done)
 * @internal
 * @brief Create a shadow thread context over a kernel task.
 *
 * This call maps a nucleus thread to the "current" Linux task running
 * in kernel space.  The priority and scheduling class of the
 * underlying Linux task are not affected; it is assumed that the
 * caller did set them appropriately before issuing the shadow mapping
 * request.
 *
 * This call immediately moves the calling kernel thread to the
 * Xenomai domain.
 *
 * @param thread The descriptor address of the new shadow thread to be
 * mapped to "current". This descriptor must have been previously
 * initialized by a call to xnthread_init().
 *
 * @param done A completion object to be signaled when @a thread is
 * fully mapped over the current Linux context, waiting for
 * xnthread_start().
 *
 * @return 0 is returned on success. Otherwise:
 *
 * - -ERESTARTSYS is returned if the current Linux task has received a
 * signal, thus preventing the final migration to the Xenomai domain
 * (i.e. in order to process the signal in the Linux domain). This
 * error should not be considered as fatal.
 *
 * - -EPERM is returned if the shadow thread has been killed before
 * the current task had a chance to return to the caller. In such a
 * case, the real-time mapping operation has failed globally, and no
 * Xenomai resource remains attached to it.
 *
 * - -EINVAL is returned if the thread control block bears the XNUSER
 * bit.
 *
 * - -EBUSY is returned if either the current Linux task or the
 * associated shadow thread is already involved in a shadow mapping.
 *
 * Calling context: This service may be called on behalf of a regular
 * kernel thread.
 *
 * Rescheduling: always.
 */
int xnshadow_map_kernel(struct xnthread *thread, struct completion *done)
{
	struct xnpersonality *personality = thread->personality;
	struct task_struct *p = current;
	int ret;
	spl_t s;

	if (xnthread_test_state(thread, XNUSER))
		return -EINVAL;

	if (xnshadow_current() || xnthread_test_state(thread, XNMAPPED))
		return -EBUSY;

	ret = enter_personality(personality);
	if (ret)
		return ret;

	thread->u_window = NULL;
	pin_to_initial_cpu(thread);

	trace_mark(xn_nucleus, shadow_map_kernel,
		   "thread %p thread_name %s pid %d priority %d",
		   thread, xnthread_name(thread), p->pid,
		   xnthread_base_priority(thread));

	xnthread_init_shadow_tcb(thread, p);
	xnthread_suspend(thread, XNRELAX, XN_INFINITE, XN_RELATIVE, NULL);
	init_threadinfo(thread);
	xnthread_set_state(thread, XNMAPPED);
	xndebug_shadow_init(thread);
	xnthread_run_handler(thread, map_thread);
	ipipe_enable_notifier(p);

	/*
	 * CAUTION: Soon after xnthread_init() has returned,
	 * xnthread_start() is commonly invoked from the root domain,
	 * therefore the call site may expect the started kernel
	 * shadow to preempt immediately. As a result of such
	 * assumption, start attributes (struct xnthread_start_attr)
	 * are often laid on the caller's stack.
	 *
	 * For this reason, we raise the completion signal to wake up
	 * the xnthread_init() caller only once the emerging thread is
	 * hardened, and __never__ before that point. Since we run
	 * over the Xenomai domain upon return from xnshadow_harden(),
	 * we schedule a virtual interrupt handler in the root domain
	 * to signal the completion object.
	 */
	xnthread_resume(thread, XNDORMANT);
	ret = xnshadow_harden();
	wakeup_parent(done);
	xnlock_get_irqsave(&nklock, s);
	/*
	 * Make sure xnthread_start() did not slip in from another CPU
	 * while we were back from wakeup_parent().
	 */
	if (thread->entry == NULL)
		xnthread_suspend(thread, XNDORMANT,
				 XN_INFINITE, XN_RELATIVE, NULL);
	xnlock_put_irqrestore(&nklock, s);

	xnthread_test_cancel();

	xntrace_pid(xnthread_host_pid(thread),
		    xnthread_current_priority(thread));

	return ret;
}
EXPORT_SYMBOL_GPL(xnshadow_map_kernel);

void xnshadow_finalize(struct xnthread *thread)
{
	trace_mark(xn_nucleus, shadow_finalize,
		   "thread %p thread_name %s pid %d",
		   thread, xnthread_name(thread), xnthread_host_pid(thread));

	xnthread_run_handler(thread, finalize_thread);
}

static int xnshadow_sys_migrate(int domain)
{
	struct xnthread *thread = xnshadow_current();

	if (ipipe_root_p) {
		if (domain == XENOMAI_XENO_DOMAIN) {
			if (thread == NULL)
				return -EPERM;
			/*
			 * Paranoid: a corner case where userland
			 * fiddles with SIGSHADOW while the target
			 * thread is still waiting to be started.
			 */
			if (xnthread_test_state(thread, XNDORMANT))
				return 0;

			return xnshadow_harden() ? : 1;
		}
		return 0;
	}

	/* ipipe_current_domain != ipipe_root_domain */
	if (domain == XENOMAI_LINUX_DOMAIN) {
		xnshadow_relax(0, 0);
		return 1;
	}

	return 0;
}

static void stringify_feature_set(unsigned long fset, char *buf, int size)
{
	unsigned long feature;
	int nc, nfeat;

	*buf = '\0';

	for (feature = 1, nc = nfeat = 0; fset != 0 && size > 0; feature <<= 1) {
		if (fset & feature) {
			nc = snprintf(buf, size, "%s%s",
				      nfeat > 0 ? " " : "",
				      get_feature_label(feature));
			nfeat++;
			size -= nc;
			buf += nc;
			fset &= ~feature;
		}
	}
}

static int mayday_map(struct file *filp, struct vm_area_struct *vma)
{
	vma->vm_pgoff = (unsigned long)mayday_page >> PAGE_SHIFT;
	return xnheap_remap_vm_page(vma, vma->vm_start,
				    (unsigned long)mayday_page);
}

#ifndef CONFIG_MMU
static unsigned long mayday_unmapped_area(struct file *file,
					  unsigned long addr,
					  unsigned long len,
					  unsigned long pgoff,
					  unsigned long flags)
{
	return (unsigned long)mayday_page;
}
#else
#define mayday_unmapped_area  NULL
#endif

static struct file_operations mayday_fops = {
	.mmap = mayday_map,
	.get_unmapped_area = mayday_unmapped_area
};

static unsigned long map_mayday_page(struct task_struct *p)
{
	const struct file_operations *old_fops;
	unsigned long u_addr;
	struct file *filp;

	filp = filp_open(XNHEAP_DEV_NAME, O_RDONLY, 0);
	if (IS_ERR(filp))
		return 0;

	old_fops = filp->f_op;
	filp->f_op = &mayday_fops;
	u_addr = vm_mmap(filp, 0, PAGE_SIZE, PROT_EXEC|PROT_READ, MAP_SHARED, 0);
	filp->f_op = (typeof(filp->f_op))old_fops;
	filp_close(filp, p->files);

	return IS_ERR_VALUE(u_addr) ? 0UL : u_addr;
}

/* nklock locked, irqs off */
void xnshadow_call_mayday(struct xnthread *thread, int sigtype)
{
	struct task_struct *p = xnthread_host_task(thread);

	/* Mayday traps are available to userland threads only. */
	XENO_BUGON(NUCLEUS, !xnthread_test_state(thread, XNUSER));
	xnthread_set_info(thread, XNKICKED);
	xnshadow_send_sig(thread, SIGDEBUG, sigtype);
	xnarch_call_mayday(p);
}
EXPORT_SYMBOL_GPL(xnshadow_call_mayday);

static int xnshadow_sys_mayday(void)
{
	struct xnthread *cur;

	cur = xnshadow_current();
	if (likely(cur)) {
		/*
		 * If the thread was kicked by the watchdog, this
		 * syscall we have just forced on it via the mayday
		 * escape will cause it to relax. See
		 * handle_head_syscall().
		 */
		xnarch_fixup_mayday(xnthread_archtcb(cur), cur->regs);

		/*
		 * Return whatever value xnarch_fixup_mayday set for
		 * this register, in order not to undo what
		 * xnarch_fixup_mayday did.
		 */
		return __xn_reg_rval(cur->regs);
	}

	printk(XENO_WARN "MAYDAY received from invalid context %s[%d]\n",
	       current->comm, current->pid);

	return -EPERM;
}

static inline int mayday_init_page(void)
{
	mayday_page = vmalloc(PAGE_SIZE);
	if (mayday_page == NULL) {
		printk(XENO_ERR "can't alloc MAYDAY page\n");
		return -ENOMEM;
	}

	xnarch_setup_mayday_page(mayday_page);

	return 0;
}

static inline void mayday_cleanup_page(void)
{
	if (mayday_page)
		vfree(mayday_page);
}

static int handle_mayday_event(struct pt_regs *regs)
{
	struct xnthread *thread = xnshadow_current();
	struct xnarchtcb *tcb = xnthread_archtcb(thread);
	struct xnsys_ppd *sys_ppd;

	XENO_BUGON(NUCLEUS, !xnthread_test_state(thread, XNUSER));

	/* We enter the mayday handler with hw IRQs off. */
	xnlock_get(&nklock);
	sys_ppd = xnsys_ppd_get(0);
	xnlock_put(&nklock);

	xnarch_handle_mayday(tcb, regs, sys_ppd->mayday_addr);

	return EVENT_PROPAGATE;
}

static inline int raise_cap(int cap)
{
	struct cred *new;

	new = prepare_creds();
	if (new == NULL)
		return -ENOMEM;

	cap_raise(new->cap_effective, cap);

	return commit_creds(new);
}

static int xnshadow_sys_bind(unsigned int magic,
			     struct xnsysinfo __user *u_breq)
{
	struct xnshadow_ppd *ppd = NULL, *sys_ppd = NULL;
	struct xnpersonality *personality;
	unsigned long featreq, featmis;
	int muxid, abirev, ret;
	struct xnbindreq breq;
	struct xnfeatinfo *f;
	spl_t s;

	if (__xn_safe_copy_from_user(&breq, u_breq, sizeof(breq)))
		return -EFAULT;

	f = &breq.feat_ret;
	featreq = breq.feat_req;
	featmis = (~XENOMAI_FEAT_DEP & (featreq & XENOMAI_FEAT_MAN));
	abirev = breq.abi_rev;

	/*
	 * Pass back the supported feature set and the ABI revision
	 * level to user-space.
	 */
	f->feat_all = XENOMAI_FEAT_DEP;
	stringify_feature_set(XENOMAI_FEAT_DEP, f->feat_all_s,
			      sizeof(f->feat_all_s));
	f->feat_man = featreq & XENOMAI_FEAT_MAN;
	stringify_feature_set(f->feat_man, f->feat_man_s,
			      sizeof(f->feat_man_s));
	f->feat_mis = featmis;
	stringify_feature_set(featmis, f->feat_mis_s,
			      sizeof(f->feat_mis_s));
	f->feat_req = featreq;
	stringify_feature_set(featreq, f->feat_req_s,
			      sizeof(f->feat_req_s));
	f->feat_abirev = XENOMAI_ABI_REV;
	collect_arch_features(f);

	if (__xn_safe_copy_to_user(u_breq, &breq, sizeof(breq)))
		return -EFAULT;

	/*
	 * If some mandatory features the user-space code relies on
	 * are missing at kernel level, we cannot go further.
	 */
	if (featmis)
		return -EINVAL;

	if (!check_abi_revision(abirev))
		return -ENOEXEC;

	if (!capable(CAP_SYS_NICE) &&
	    (xn_gid_arg == -1 || !in_group_p(KGIDT_INIT(xn_gid_arg))))
		return -EPERM;

	/* Raise capabilities for the caller in case they are lacking yet. */
	raise_cap(CAP_SYS_NICE);
	raise_cap(CAP_IPC_LOCK);
	raise_cap(CAP_SYS_RAWIO);

	xnlock_get_irqsave(&nklock, s);

	for (muxid = 1; muxid < PERSONALITIES_NR; muxid++) {
		personality = personalities[muxid];
		if (personality && personality->magic == magic)
			goto do_bind;
	}

	xnlock_put_irqrestore(&nklock, s);

	return -ESRCH;

do_bind:

	sys_ppd = ppd_lookup(0, current->mm);

	xnlock_put_irqrestore(&nklock, s);

	if (sys_ppd)
		goto muxid_eventcb;

	sys_ppd = personalities[user_muxid]->ops.attach_process();
	if (IS_ERR(sys_ppd))
		return PTR_ERR(sys_ppd);

	if (sys_ppd == NULL)
		goto muxid_eventcb;

	sys_ppd->key.muxid = 0;
	sys_ppd->key.mm = current->mm;
	if (ppd_insert(sys_ppd) == -EBUSY) {
		/* In case of concurrent binding (which can not happen with
		   Xenomai libraries), detach right away the second ppd. */
		personalities[user_muxid]->ops.detach_process(sys_ppd);
		sys_ppd = NULL;
	}

	if (personality->module && !try_module_get(personality->module)) {
		ret = -ENOSYS;
		goto fail_destroy_sys_ppd;
	}

muxid_eventcb:

	xnlock_get_irqsave(&nklock, s);
	ppd = ppd_lookup(muxid, current->mm);
	xnlock_put_irqrestore(&nklock, s);

	/* protect from the same process binding several times. */
	if (ppd)
		return muxid;

	ppd = personality->ops.attach_process();
	if (IS_ERR(ppd)) {
		ret = PTR_ERR(ppd);
		goto fail_destroy_sys_ppd;
	}

	if (ppd == NULL)
		return muxid;

	ppd->key.muxid = muxid;
	ppd->key.mm = current->mm;

	if (ppd_insert(ppd) == -EBUSY) {
		/*
		 * In case of concurrent binding (which can not happen
		 * with Xenomai libraries), detach right away the
		 * second ppd.
		 */
		personality->ops.detach_process(ppd);
		ppd = NULL;
	}

	return muxid;

 fail_destroy_sys_ppd:
	if (sys_ppd) {
		ppd_remove(sys_ppd);
		personalities[user_muxid]->ops.detach_process(sys_ppd);
	}

	return ret;
}

static int xnshadow_sys_info(int muxid, struct xnsysinfo __user *u_info)
{
	struct xnsysinfo info;

	if (muxid < 0 || muxid > PERSONALITIES_NR ||
	    personalities[muxid] == NULL)
		return -EINVAL;

	info.clockfreq = xnarch_machdata.clock_freq;
	info.vdso = xnheap_mapped_offset(&xnsys_ppd_get(1)->sem_heap,
					 nkvdso);

	return __xn_safe_copy_to_user(u_info, &info, sizeof(info));
}

static int xnshadow_sys_trace(int op, unsigned long a1,
			      unsigned long a2, unsigned long a3)
{
	int ret = -ENOSYS;

	switch (op) {
	case __xntrace_op_max_begin:
		ret = xntrace_max_begin(a1);
		break;

	case __xntrace_op_max_end:
		ret = xntrace_max_end(a1);
		break;

	case __xntrace_op_max_reset:
		ret = xntrace_max_reset();
		break;

	case __xntrace_op_user_start:
		ret = xntrace_user_start();
		break;

	case __xntrace_op_user_stop:
		ret = xntrace_user_stop(a1);
		break;

	case __xntrace_op_user_freeze:
		ret = xntrace_user_freeze(a1, a2);
		break;

	case __xntrace_op_special:
		ret = xntrace_special(a1 & 0xFF, a2);
		break;

	case __xntrace_op_special_u64:
		ret = xntrace_special_u64(a1 & 0xFF,
					  (((u64) a2) << 32) | a3);
		break;
	}
	return ret;
}

static int xnshadow_sys_heap_info(struct xnheap_desc __user *u_hd,
				 unsigned int heap_nr)
{
	struct xnheap_desc hd;
	struct xnheap *heap;

	switch(heap_nr) {
	case XNHEAP_PROC_PRIVATE_HEAP:
	case XNHEAP_PROC_SHARED_HEAP:
		heap = &xnsys_ppd_get(heap_nr)->sem_heap;
		break;
	case XNHEAP_SYS_HEAP:
		heap = &kheap;
		break;
	default:
		return -EINVAL;
	}

	hd.handle = (unsigned long)heap;
	hd.size = xnheap_extentsize(heap);
	hd.area = xnheap_base_memory(heap);
	hd.used = xnheap_used_mem(heap);

	return __xn_safe_copy_to_user(u_hd, &hd, sizeof(*u_hd));
}

static int xnshadow_sys_current(xnhandle_t __user *u_handle)
{
	struct xnthread *cur = xnshadow_current();

	if (cur == NULL)
		return -EPERM;

	return __xn_safe_copy_to_user(u_handle, &xnthread_handle(cur),
				      sizeof(*u_handle));
}

static int xnshadow_sys_current_info(struct xnthread_info __user *u_info)
{
	struct xnthread *cur = xnshadow_current();
	struct xnthread_info info;
	xnticks_t raw_exectime;
	int i;

	if (cur == NULL)
		return -EPERM;

	info.state = xnthread_state_flags(cur);
	info.bprio = xnthread_base_priority(cur);
	info.cprio = xnthread_current_priority(cur);
	info.cpu = xnsched_cpu(xnthread_sched(cur));
	for (i = 0, info.affinity = 0; i < BITS_PER_LONG; i++)
		if (xnthread_affine_p(cur, i))
			info.affinity |= 1UL << i;
	info.relpoint = xntimer_get_date(&cur->ptimer);
	raw_exectime = xnthread_get_exectime(cur) +
		xnstat_exectime_now() - xnthread_get_lastswitch(cur);
	info.exectime = xnclock_ticks_to_ns(&nkclock, raw_exectime);
	info.modeswitches = xnstat_counter_get(&cur->stat.ssw);
	info.ctxswitches = xnstat_counter_get(&cur->stat.csw);
	info.pagefaults = xnstat_counter_get(&cur->stat.pf);
	info.syscalls = xnstat_counter_get(&cur->stat.xsc);
	strcpy(info.name, xnthread_name(cur));

	return __xn_safe_copy_to_user(u_info, &info, sizeof(*u_info));
}

static int xnshadow_sys_backtrace(int nr, unsigned long __user *u_backtrace,
				  int reason)
{
	xndebug_trace_relax(nr, u_backtrace, reason);
	return 0;
}

static int xnshadow_sys_serialdbg(const char __user *u_msg, int len)
{
	char buf[128];
	int n;

	while (len > 0) {
		n = len;
		if (n > sizeof(buf))
			n = sizeof(buf);
		if (__xn_safe_copy_from_user(buf, u_msg, n))
			return -EFAULT;
		__ipipe_serial_debug("%.*s", n, buf);
		u_msg += n;
		len -= n;
	}

	return 0;
}

static void post_ppd_release(struct xnheap *h)
{
	struct xnsys_ppd *p = container_of(h, struct xnsys_ppd, sem_heap);
	kfree(p);
}

static inline char *get_exe_path(struct task_struct *p)
{
	struct file *exe_file;
	char *pathname, *buf;
	struct mm_struct *mm;
	struct path path;

	/*
	 * PATH_MAX is fairly large, and in any case won't fit on the
	 * caller's stack happily; since we are mapping a shadow,
	 * which is a heavyweight operation anyway, let's pick the
	 * memory from the page allocator.
	 */
	buf = (char *)__get_free_page(GFP_TEMPORARY);
	if (buf == NULL)
		return ERR_PTR(-ENOMEM);

	mm = get_task_mm(p);
	if (mm == NULL) {
		pathname = "vmlinux";
		goto copy;	/* kernel thread */
	}

	exe_file = get_mm_exe_file(mm);
	mmput(mm);
	if (exe_file == NULL) {
		pathname = ERR_PTR(-ENOENT);
		goto out;	/* no luck. */
	}

	path = exe_file->f_path;
	path_get(&exe_file->f_path);
	fput(exe_file);
	pathname = d_path(&path, buf, PATH_MAX);
	path_put(&path);
	if (IS_ERR(pathname))
		goto out;	/* mmmh... */
copy:
	/* caution: d_path() may start writing anywhere in the buffer. */
	pathname = kstrdup(pathname, GFP_KERNEL);
out:
	free_page((unsigned long)buf);

	return pathname;
}

static struct xnshadow_ppd *user_process_attach(void)
{
	struct xnsys_ppd *p;
	char *exe_path;
	int ret;

	p = kmalloc(sizeof(*p), GFP_KERNEL);
	if (p == NULL)
		return ERR_PTR(-ENOMEM);

	ret = xnheap_init_mapped(&p->sem_heap,
				 CONFIG_XENO_OPT_SEM_HEAPSZ * 1024,
				 XNARCH_SHARED_HEAP_FLAGS);
	if (ret) {
		kfree(p);
		return ERR_PTR(ret);
	}

	xnheap_set_label(&p->sem_heap,
			 "private sem heap [%d]", current->pid);

	p->mayday_addr = map_mayday_page(current);
	if (p->mayday_addr == 0) {
		printk(XENO_WARN
		       "%s[%d] cannot map MAYDAY page\n",
		       current->comm, current->pid);
		kfree(p);
		return ERR_PTR(-ENOMEM);
	}

	exe_path = get_exe_path(current);
	if (IS_ERR(exe_path)) {
		printk(XENO_WARN
		       "%s[%d] can't find exe path\n",
		       current->comm, current->pid);
		exe_path = NULL; /* Not lethal, but weird. */
	}
	p->exe_path = exe_path;
	atomic_set(&p->refcnt, 1);
	atomic_inc(&personalities[user_muxid]->refcnt);

	return &p->ppd;
}

static void user_process_detach(struct xnshadow_ppd *ppd)
{
	struct xnsys_ppd *p;

	p = container_of(ppd, struct xnsys_ppd, ppd);
	if (p->exe_path)
		kfree(p->exe_path);
	xnheap_destroy_mapped(&p->sem_heap, post_ppd_release, NULL);
	atomic_dec(&personalities[user_muxid]->refcnt);
}

static struct xnsyscall user_syscalls[] = {
	SKINCALL_DEF(sc_nucleus_migrate, xnshadow_sys_migrate, current),
	SKINCALL_DEF(sc_nucleus_arch, xnarch_local_syscall, any),
	SKINCALL_DEF(sc_nucleus_bind, xnshadow_sys_bind, lostage),
	SKINCALL_DEF(sc_nucleus_info, xnshadow_sys_info, lostage),
	SKINCALL_DEF(sc_nucleus_trace, xnshadow_sys_trace, any),
	SKINCALL_DEF(sc_nucleus_heap_info, xnshadow_sys_heap_info, lostage),
	SKINCALL_DEF(sc_nucleus_current, xnshadow_sys_current, any),
	SKINCALL_DEF(sc_nucleus_current_info, xnshadow_sys_current_info, shadow),
	SKINCALL_DEF(sc_nucleus_mayday, xnshadow_sys_mayday, oneway),
	SKINCALL_DEF(sc_nucleus_backtrace, xnshadow_sys_backtrace, current),
	SKINCALL_DEF(sc_nucleus_serialdbg, xnshadow_sys_serialdbg, any),
};

static struct xnpersonality user_personality = {
	.name = "user",
	.magic = 0,
	.nrcalls = ARRAY_SIZE(user_syscalls),
	.syscalls = user_syscalls,
	.ops = {
		.attach_process = user_process_attach,
		.detach_process = user_process_detach,
	},
};

void xnshadow_send_sig(struct xnthread *thread, int sig, int arg)
{
	struct lostage_signal sigwork = {
		.work = {
			.size = sizeof(sigwork),
			.handler = lostage_task_signal,
		},
		.task = xnthread_host_task(thread),
		.signo = sig,
		.sigval = arg,
	};

	ipipe_post_work_root(&sigwork, work);
}
EXPORT_SYMBOL_GPL(xnshadow_send_sig);

/**
 * @fn int xnshadow_register_personality(struct xnpersonality *personality)
 * @internal
 * @brief Register a new interface personality.
 *
 * - ops->attach_process() is called when a user-space process binds
 *   to the personality, on behalf of one of its threads. The
 *   attach_process() handler may return:
 *
 *   . a pointer to a xnshadow_ppd structure, representing the context
 *   of the calling process for this personality;
 *
 *   . a NULL pointer, meaning that no per-process structure should be
 *   attached to this process for this personality;
 *
 *   . ERR_PTR(negative value) indicating an error, the binding
 *   process will then abort.
 *
 * - ops->detach() is called on behalf of an exiting user-space
 *   process which has previously attached to the personality. This
 *   handler is passed a pointer to the per-process data received
 *   earlier from the ops->attach_process() handler.
 */
int xnshadow_register_personality(struct xnpersonality *personality)
{
	int muxid;
	spl_t s;

	down(&registration_mutex);

	xnlock_get_irqsave(&nklock, s);

	for (muxid = 0; muxid < PERSONALITIES_NR; muxid++) {
		if (personalities[muxid] == NULL) {
			atomic_set(&personality->refcnt, 0);
			personalities[muxid] = personality;
			break;
		}
	}

	xnlock_put_irqrestore(&nklock, s);

	if (muxid >= PERSONALITIES_NR)
		muxid = -EAGAIN;

	up(&registration_mutex);

	return muxid;

}
EXPORT_SYMBOL_GPL(xnshadow_register_personality);

/*
 * xnshadow_unregister_personality() -- Unregister an interface
 * personality.
 */
int xnshadow_unregister_personality(int muxid)
{
	struct xnpersonality *personality;
	int ret = 0;
	spl_t s;

	if (muxid < 0 || muxid >= PERSONALITIES_NR)
		return -EINVAL;

	down(&registration_mutex);

	xnlock_get_irqsave(&nklock, s);

	personality = personalities[muxid];
	if (atomic_read(&personality->refcnt) > 0)
		ret = -EBUSY;
	else
		personalities[muxid] = NULL;

	xnlock_put_irqrestore(&nklock, s);

	up(&registration_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(xnshadow_unregister_personality);

/**
 * Return the per-process data attached to the calling process.
 *
 * This service returns the per-process data attached to the calling
 * process for the personality whose muxid is @a muxid. It must be
 * called with nklock locked, irqs off.
 *
 * See xnshadow_register_personality() documentation for information
 * on the way to attach a per-process data to a process.
 *
 * @param muxid the personality muxid.
 *
 * @return the per-process data if the current context is a user-space
 * process; @return NULL otherwise.
 *
 */
struct xnshadow_ppd *xnshadow_ppd_get(unsigned int muxid)
{
	struct xnthread *curr = xnsched_current_thread();

	if (xnthread_test_state(curr, XNROOT|XNUSER))
		return ppd_lookup(muxid, xnshadow_current_mm() ?: current->mm);

	return NULL;
}
EXPORT_SYMBOL_GPL(xnshadow_ppd_get);

/**
 * Stack a new personality over an existing thread.
 *
 * This service registers @a thread as a member of the additional
 * personality @a next.
 *
 * @param thread the affected thread.
 *
 * @param next the additional personality to declare for @a thread.
 *
 * @return A pointer to the previous personality. The caller should
 * save this pointer for unstacking @a next when applicable via a call
 * to xnshadow_pop_personality().
 */
struct xnpersonality *
xnshadow_push_personality(struct xnthread *thread,
			  struct xnpersonality *next)
{
	struct xnpersonality *prev = thread->personality;

	thread->personality = next;
	enter_personality(next);

	return prev;
}
EXPORT_SYMBOL_GPL(xnshadow_push_personality);

/**
 * Pop the topmost personality from a thread.
 *
 * This service unregisters @a thread from the topmost personality it
 * is a member of.
 *
 * @param thread the affected thread.
 *
 * @param prev the previous personality in effect for @a thread prior
 * to pushing the topmost one, as returned by the latest call to
 * xnshadow_push_personality().
 */
void xnshadow_pop_personality(struct xnthread *thread,
			      struct xnpersonality *prev)
{
	struct xnpersonality *old = thread->personality;

	thread->personality = prev;
	leave_personality(old);
}
EXPORT_SYMBOL_GPL(xnshadow_pop_personality);

static int handle_head_syscall(struct ipipe_domain *ipd, struct pt_regs *regs)
{
	int muxid, muxop, switched, ret, sigs;
	struct xnpersonality *personality;
	struct xnthread *thread;
	unsigned long sysflags;
	struct xnsyscall *sc;

	thread = xnshadow_current();
	if (thread)
		thread->regs = regs;

	if (!__xn_reg_mux_p(regs))
		goto linux_syscall;

	muxid = __xn_mux_id(regs);
	muxop = __xn_mux_op(regs);

	trace_mark(xn_nucleus, syscall_histage_entry,
		   "thread %p thread_name %s muxid %d muxop %d",
		   thread, thread ? xnthread_name(thread) : NULL,
		   muxid, muxop);

	if (muxid < 0 || muxid >= PERSONALITIES_NR || muxop < 0)
		goto bad_syscall;

	personality = personalities[muxid];
	if (muxop >= personality->nrcalls)
		goto bad_syscall;

	sc = personality->syscalls + muxop;
	sysflags = sc->flags;

	/*
	 * Executing Xenomai services requires CAP_SYS_NICE, except
	 * for sc_nucleus_bind which does its own checks.
	 */
	if (unlikely((thread == NULL && (sysflags & __xn_exec_shadow) != 0) ||
		     (!cap_raised(current_cap(), CAP_SYS_NICE) &&
		      muxid == 0 && muxop == sc_nucleus_bind))) {
		if (XENO_DEBUG(NUCLEUS))
			printk(XENO_WARN
			       "non-shadow %s[%d] was denied a real-time call (%s/%d)\n",
			       current->comm, current->pid, personality->name, muxop);
		__xn_error_return(regs, -EPERM);
		goto ret_handled;
	}

	if (sysflags & __xn_exec_conforming)
		/*
		 * If the conforming exec bit is set, turn the exec
		 * bitmask for the syscall into the most appropriate
		 * setup for the caller, i.e. Xenomai domain for
		 * shadow threads, Linux otherwise.
		 */
		sysflags |= (thread ? __xn_exec_histage : __xn_exec_lostage);

	/*
	 * Here we have to dispatch the syscall execution properly,
	 * depending on:
	 *
	 * o Whether the syscall must be run into the Linux or Xenomai
	 * domain, or indifferently in the current Xenomai domain.
	 *
	 * o Whether the caller currently runs in the Linux or Xenomai
	 * domain.
	 */
	switched = 0;
restart:
	/*
	 * Process adaptive syscalls by restarting them in the
	 * opposite domain.
	 */
	if (sysflags & __xn_exec_lostage) {
		/* Syscall must run into the Linux domain. */
		if (ipd == &xnarch_machdata.domain) {
			/*
			 * Request originates from the Xenomai domain:
			 * just relax the caller and execute the
			 * syscall immediately after.
			 */
			xnshadow_relax(1, SIGDEBUG_MIGRATE_SYSCALL);
			switched = 1;
		} else
			/*
			 * Request originates from the Linux domain:
			 * propagate the event to our Linux-based
			 * handler, so that the syscall is executed
			 * from there.
			 */
			return EVENT_PROPAGATE;
	} else if (sysflags & (__xn_exec_histage | __xn_exec_current)) {
		/*
		 * Syscall must be processed either by Xenomai, or by
		 * the calling domain.
		 */
		if (ipd != &xnarch_machdata.domain)
			/*
			 * Request originates from the Linux domain:
			 * propagate the event to our Linux-based
			 * handler, so that the caller is hardened and
			 * the syscall is eventually executed from
			 * there.
			 */
			return EVENT_PROPAGATE;
		/*
		 * Request originates from the Xenomai domain: run the
		 * syscall immediately.
		 */
	}

	ret = sc->svc(__xn_reg_arglist(regs));
	if (ret == -ENOSYS && (sysflags & __xn_exec_adaptive) != 0) {
		if (switched) {
			switched = 0;
			ret = xnshadow_harden();
			if (ret)
				goto done;
		}

		sysflags ^=
		    (__xn_exec_lostage | __xn_exec_histage |
		     __xn_exec_adaptive);
		goto restart;
	}
done:
	__xn_status_return(regs, ret);
	sigs = 0;
	if (!xnsched_root_p()) {
		if (signal_pending(current) ||
		    xnthread_test_info(thread, XNKICKED)) {
			sigs = 1;
			request_syscall_restart(thread, regs, sysflags);
		} else if (xnthread_test_state(thread, XNWEAK) &&
			   xnthread_get_rescnt(thread) == 0) {
			if (switched)
				switched = 0;
			else
				xnshadow_relax(0, 0);
		}
	}
	if (!sigs && (sysflags & __xn_exec_switchback) != 0 && switched)
		xnshadow_harden(); /* -EPERM will be trapped later if needed. */

ret_handled:
	/* Update the stats and userland-visible state. */
	if (thread) {
		xnstat_counter_inc(&thread->stat.xsc);
		xnthread_sync_window(thread);
	}

	trace_mark(xn_nucleus, syscall_histage_exit,
		   "ret %ld", __xn_reg_rval(regs));
	return EVENT_STOP;

linux_syscall:
	if (xnsched_root_p())
		/*
		 * The call originates from the Linux domain, either
		 * from a relaxed shadow or from a regular Linux task;
		 * just propagate the event so that we will fall back
		 * to linux_sysentry().
		 */
		return EVENT_PROPAGATE;

	/*
	 * From now on, we know that we have a valid shadow thread
	 * pointer.
	 *
	 * The current syscall will eventually fall back to the Linux
	 * syscall handler if our Linux domain handler does not
	 * intercept it. Before we let it go, ensure that the current
	 * thread has properly entered the Linux domain.
	 */
	xnshadow_relax(1, SIGDEBUG_MIGRATE_SYSCALL);

	return EVENT_PROPAGATE;

bad_syscall:
	printk(XENO_WARN "bad syscall %ld/%ld\n",
	       __xn_mux_id(regs), __xn_mux_op(regs));
	
	__xn_error_return(regs, -ENOSYS);

	return EVENT_STOP;
}

static int handle_root_syscall(struct ipipe_domain *ipd, struct pt_regs *regs)
{
	int muxid, muxop, sysflags, switched, ret, sigs;
	struct xnthread *thread;
	struct xnsyscall *sc;

	/*
	 * Catch cancellation requests pending for user shadows
	 * running mostly in secondary mode, i.e. XNWEAK. In that
	 * case, we won't run request_syscall_restart() that
	 * frequently, so check for cancellation here.
	 */
	xnthread_test_cancel();

	thread = xnshadow_current();
	if (thread)
		thread->regs = regs;

	if (!__xn_reg_mux_p(regs))
		/* Fall back to Linux syscall handling. */
		return EVENT_PROPAGATE;

	/*
	 * muxid and muxop have already been checked in the Xenomai domain
	 * handler.
	 */
	muxid = __xn_mux_id(regs);
	muxop = __xn_mux_op(regs);

	trace_mark(xn_nucleus, syscall_lostage_entry,
		   "thread %p thread_name %s muxid %d muxop %d",
		   xnsched_current_thread(),
		   xnthread_name(xnsched_current_thread()),
		   muxid, muxop);

	/* Processing a Xenomai syscall. */

	sc = personalities[muxid]->syscalls + muxop;
	sysflags = sc->flags;

	if ((sysflags & __xn_exec_conforming) != 0)
		sysflags |= (thread ? __xn_exec_histage : __xn_exec_lostage);
restart:
	/*
	 * Process adaptive syscalls by restarting them in the
	 * opposite domain.
	 */
	if (sysflags & __xn_exec_histage) {
		/*
		 * This request originates from the Linux domain and
		 * must be run into the Xenomai domain: harden the
		 * caller and execute the syscall.
		 */
		ret = xnshadow_harden();
		if (ret) {
			__xn_error_return(regs, ret);
			goto ret_handled;
		}
		switched = 1;
	} else
		/*
		 * We want to run the syscall in the Linux domain.
		 */
		switched = 0;

	ret = sc->svc(__xn_reg_arglist(regs));
	if (ret == -ENOSYS && (sysflags & __xn_exec_adaptive) != 0) {
		if (switched) {
			switched = 0;
			xnshadow_relax(1, SIGDEBUG_MIGRATE_SYSCALL);
		}

		sysflags ^=
		    (__xn_exec_lostage | __xn_exec_histage |
		     __xn_exec_adaptive);
		goto restart;
	}

	__xn_status_return(regs, ret);

	sigs = 0;
	if (!xnsched_root_p()) {
		/*
		 * We may have gained a shadow TCB from the syscall we
		 * just invoked, so make sure to fetch it.
		 */
		thread = xnshadow_current();
		if (signal_pending(current)) {
			sigs = 1;
			request_syscall_restart(thread, regs, sysflags);
		} else if (xnthread_test_state(thread, XNWEAK) &&
			   xnthread_get_rescnt(thread) == 0)
			sysflags |= __xn_exec_switchback;
	}
	if (!sigs && (sysflags & __xn_exec_switchback) != 0
	    && (switched || xnsched_primary_p()))
		xnshadow_relax(0, 0);

ret_handled:
	/* Update the stats and userland-visible state. */
	if (thread) {
		xnstat_counter_inc(&thread->stat.xsc);
		xnthread_sync_window(thread);
	}

	trace_mark(xn_nucleus, syscall_lostage_exit,
		   "ret %ld", __xn_reg_rval(regs));

	return EVENT_STOP;
}

int ipipe_syscall_hook(struct ipipe_domain *ipd, struct pt_regs *regs)
{
	if (unlikely(ipipe_root_p))
		return handle_root_syscall(ipd, regs);

	return handle_head_syscall(ipd, regs);
}

static int handle_taskexit_event(struct task_struct *p) /* p == current */
{
	struct xnpersonality *personality;
	struct xnsys_ppd *sys_ppd;
	struct xnthread *thread;
	struct mm_struct *mm;
	spl_t s;

	/*
	 * We are called for both kernel and user shadows over the
	 * root thread.
	 */
	secondary_mode_only();
	thread = xnshadow_current();
	XENO_BUGON(NUCLEUS, thread == NULL);
	personality = thread->personality;

	trace_mark(xn_nucleus, shadow_exit, "thread %p thread_name %s",
		   thread, xnthread_name(thread));

	if (xnthread_test_state(thread, XNDEBUG))
		unlock_timers();

	xnthread_run_handler(thread, exit_thread);
	/* Waiters will receive EIDRM */
	xnsynch_destroy(&thread->join_synch);
	xnsched_run();

	if (xnthread_test_state(thread, XNUSER)) {
		xnlock_get_irqsave(&nklock, s);
		sys_ppd = xnsys_ppd_get(0);
		xnlock_put_irqrestore(&nklock, s);
		xnheap_free(&sys_ppd->sem_heap, thread->u_window);
		thread->u_window = NULL;
		mm = xnshadow_current_mm();
		if (atomic_dec_and_test(&sys_ppd->refcnt))
			ppd_remove_mm(mm, detach_ppd);
	}

	/*
	 * __xnthread_cleanup() -> ... -> xnshadow_finalize(). From
	 * that point, the TCB is dropped. Be careful of not treading
	 * on stale memory within @thread.
	 */
	__xnthread_cleanup(thread);

	leave_personality(personality);
	destroy_threadinfo();

	return EVENT_PROPAGATE;
}

int xnshadow_yield(xnticks_t min, xnticks_t max)
{
	xnticks_t start;
	int ret;

	start = xnclock_read_monotonic(&nkclock);
	max += start;
	min += start;

	do {
		ret = xnsynch_sleep_on(&yield_sync, max, XN_ABSOLUTE);
		if (ret & XNBREAK)
			return -EINTR;
	} while (ret == 0 && xnclock_read_monotonic(&nkclock) < min);

	return 0;
}

static inline void signal_yield(void)
{
	spl_t s;

	if (!xnsynch_pended_p(&yield_sync))
		return;

	xnlock_get_irqsave(&nklock, s);
	if (xnsynch_pended_p(&yield_sync)) {
		xnsynch_flush(&yield_sync, 0);
		xnsched_run();
	}
	xnlock_put_irqrestore(&nklock, s);
}

static int handle_schedule_event(struct task_struct *next_task)
{
	struct task_struct *prev_task;
	struct xnthread *prev, *next;
	sigset_t pending;

	signal_yield();

	prev_task = current;
	prev = xnshadow_thread(prev_task);
	next = xnshadow_thread(next_task);
	if (next == NULL)
		goto out;

	/*
	 * Check whether we need to unlock the timers, each time a
	 * Linux task resumes from a stopped state, excluding tasks
	 * resuming shortly for entering a stopped state asap due to
	 * ptracing. To identify the latter, we need to check for
	 * SIGSTOP and SIGINT in order to encompass both the NPTL and
	 * LinuxThreads behaviours.
	 */
	if (xnthread_test_state(next, XNDEBUG)) {
		if (signal_pending(next_task)) {
			/*
			 * Do not grab the sighand lock here: it's
			 * useless, and we already own the runqueue
			 * lock, so this would expose us to deadlock
			 * situations on SMP.
			 */
			sigorsets(&pending,
				  &next_task->pending.signal,
				  &next_task->signal->shared_pending.signal);
			if (sigismember(&pending, SIGSTOP) ||
			    sigismember(&pending, SIGINT))
				goto no_ptrace;
		}
		xnthread_clear_state(next, XNDEBUG);
		unlock_timers();
	}

no_ptrace:
	if (XENO_DEBUG(NUCLEUS)) {
		if (!xnthread_test_state(next, XNRELAX)) {
			xntrace_panic_freeze();
			show_stack(xnthread_host_task(next), NULL);
			xnsys_fatal
				("hardened thread %s[%d] running in Linux domain?! "
				 "(status=0x%lx, sig=%d, prev=%s[%d])",
				 next->name, next_task->pid, xnthread_state_flags(next),
				 signal_pending(next_task), prev_task->comm, prev_task->pid);
		} else if (!(next_task->ptrace & PT_PTRACED) &&
			   /*
			    * Allow ptraced threads to run shortly in order to
			    * properly recover from a stopped state.
			    */
			   !xnthread_test_state(next, XNDORMANT)
			   && xnthread_test_state(next, XNPEND)) {
			xntrace_panic_freeze();
			show_stack(xnthread_host_task(next), NULL);
			xnsys_fatal
				("blocked thread %s[%d] rescheduled?! "
				 "(status=0x%lx, sig=%d, prev=%s[%d])",
				 next->name, next_task->pid, xnthread_state_flags(next),
				 signal_pending(next_task), prev_task->comm, prev_task->pid);
		}
	}
out:
	return EVENT_PROPAGATE;
}

static int handle_sigwake_event(struct task_struct *p)
{
	struct xnthread *thread;
	sigset_t pending;
	spl_t s;

	thread = xnshadow_thread(p);
	if (thread == NULL)
		return EVENT_PROPAGATE;

	xnlock_get_irqsave(&nklock, s);

	if ((p->ptrace & PT_PTRACED) && !xnthread_test_state(thread, XNDEBUG)) {
		/* We already own the siglock. */
		sigorsets(&pending,
			  &p->pending.signal,
			  &p->signal->shared_pending.signal);

		if (sigismember(&pending, SIGTRAP) ||
		    sigismember(&pending, SIGSTOP)
		    || sigismember(&pending, SIGINT)) {
			xnthread_set_state(thread, XNDEBUG);
			lock_timers();
		}
	}

	if (xnthread_test_state(thread, XNRELAX)) {
		xnlock_put_irqrestore(&nklock, s);
		return EVENT_PROPAGATE;
	}

	/*
	 * If kicking a shadow thread in primary mode, make sure Linux
	 * won't schedule in its mate under our feet as a result of
	 * running signal_wake_up(). The Xenomai scheduler must remain
	 * in control for now, until we explicitly relax the shadow
	 * thread to allow for processing the pending signals. Make
	 * sure we keep the additional state flags unmodified so that
	 * we don't break any undergoing ptrace.
	 */
	if (p->state & (TASK_INTERRUPTIBLE|TASK_UNINTERRUPTIBLE))
		set_task_state(p, p->state | TASK_NOWAKEUP);

	force_wakeup(thread);

	xnsched_run();

	xnlock_put_irqrestore(&nklock, s);

	return EVENT_PROPAGATE;
}

static int handle_cleanup_event(struct mm_struct *mm)
{
	struct xnsys_ppd *sys_ppd;
	struct xnthread *thread;
	struct mm_struct *old;
	spl_t s;

	/* We are NOT called for exiting kernel shadows. */

	old = xnshadow_swap_mm(mm);

	xnlock_get_irqsave(&nklock, s);
	sys_ppd = xnsys_ppd_get(0);
	xnlock_put_irqrestore(&nklock, s);
	if (sys_ppd != &__xnsys_global_ppd) {
		/*
		 * Detect a userland shadow running exec(), i.e. still
		 * attached to the current linux task (no prior
		 * destroy_threadinfo). In this case, we emulate a
		 * task exit, since the Xenomai binding shall not
		 * survive the exec() syscall. Since the process will
		 * keep on running though, we have to disable the
		 * event notifier manually for it.
		 */
		thread = xnshadow_current();
		if (thread && (current->flags & PF_EXITING) == 0) {
			handle_taskexit_event(current);
			ipipe_disable_notifier(current);
		}
		if (atomic_dec_and_test(&sys_ppd->refcnt))
			ppd_remove_mm(mm, detach_ppd);
	}

	xnshadow_swap_mm(old);

	return EVENT_PROPAGATE;
}

#ifdef CONFIG_XENO_OPT_HOSTRT

static IPIPE_DEFINE_SPINLOCK(__hostrtlock);

static int handle_hostrt_event(struct ipipe_hostrt_data *hostrt)
{
	unsigned long flags;
	urwstate_t tmp;

	/*
	 * The locking strategy is twofold:
	 * - The spinlock protects against concurrent updates from within the
	 *   Linux kernel and against preemption by Xenomai
	 * - The unsynced R/W block is for lockless read-only access.
	 */
	spin_lock_irqsave(&__hostrtlock, flags);

	unsynced_write_block(&tmp, &nkvdso->hostrt_data.lock) {
		nkvdso->hostrt_data.live = 1;
		nkvdso->hostrt_data.cycle_last = hostrt->cycle_last;
		nkvdso->hostrt_data.mask = hostrt->mask;
		nkvdso->hostrt_data.mult = hostrt->mult;
		nkvdso->hostrt_data.shift = hostrt->shift;
		nkvdso->hostrt_data.wall_time_sec = hostrt->wall_time_sec;
		nkvdso->hostrt_data.wall_time_nsec = hostrt->wall_time_nsec;
		nkvdso->hostrt_data.wall_to_monotonic = hostrt->wall_to_monotonic;
	}

	spin_unlock_irqrestore(&__hostrtlock, flags);

	return EVENT_PROPAGATE;
}

static inline void init_hostrt(void)
{
	unsynced_rw_init(&nkvdso->hostrt_data.lock);
	nkvdso->hostrt_data.live = 0;
}

#else

struct ipipe_hostrt_data;

static inline int handle_hostrt_event(struct ipipe_hostrt_data *hostrt)
{
	return EVENT_PROPAGATE;
}

static inline void init_hostrt(void) { }

#endif

int ipipe_kevent_hook(int kevent, void *data)
{
	int ret;

	switch (kevent) {
	case IPIPE_KEVT_SCHEDULE:
		ret = handle_schedule_event(data);
		break;
	case IPIPE_KEVT_SIGWAKE:
		ret = handle_sigwake_event(data);
		break;
	case IPIPE_KEVT_EXIT:
		ret = handle_taskexit_event(data);
		break;
	case IPIPE_KEVT_CLEANUP:
		ret = handle_cleanup_event(data);
		break;
	case IPIPE_KEVT_HOSTRT:
		ret = handle_hostrt_event(data);
		break;
	case IPIPE_KEVT_SETAFFINITY:
		ret = handle_setaffinity_event(data);
		break;
	default:
		ret = EVENT_PROPAGATE;
	}

	return ret;
}

static inline int handle_exception(struct ipipe_trap_data *d)
{
	struct xnsched *sched;
	struct xnthread *thread;

	sched = xnsched_current();
	thread = sched->curr;

	if (xnthread_test_state(thread, XNROOT))
		return 0;

	trace_mark(xn_nucleus, thread_fault,
		   "thread %p thread_name %s ip %p type 0x%x",
		   thread, xnthread_name(thread),
		   (void *)xnarch_fault_pc(d),
		   xnarch_fault_trap(d));

	if (xnarch_fault_fpu_p(d)) {
#ifdef CONFIG_XENO_HW_FPU
		/* FPU exception received in primary mode. */
		if (xnarch_handle_fpu_fault(sched->fpuholder, thread, d)) {
			sched->fpuholder = thread;
			return 1;
		}
#endif /* CONFIG_XENO_HW_FPU */
		print_symbol("invalid use of FPU in Xenomai context at %s\n",
			     xnarch_fault_pc(d));
	}

	/*
	 * If we experienced a trap on behalf of a shadow thread
	 * running in primary mode, move it to the Linux domain,
	 * leaving the kernel process the exception.
	 */
	thread->regs = xnarch_fault_regs(d);

#if XENO_DEBUG(NUCLEUS)
	if (!user_mode(d->regs)) {
		xntrace_panic_freeze();
		printk(XENO_WARN
		       "switching %s to secondary mode after exception #%u in "
		       "kernel-space at 0x%lx (pid %d)\n", thread->name,
		       xnarch_fault_trap(d),
		       xnarch_fault_pc(d),
		       xnthread_host_pid(thread));
		xntrace_panic_dump();
	} else if (xnarch_fault_notify(d)) /* Don't report debug traps */
		printk(XENO_WARN
		       "switching %s to secondary mode after exception #%u from "
		       "user-space at 0x%lx (pid %d)\n", thread->name,
		       xnarch_fault_trap(d),
		       xnarch_fault_pc(d),
		       xnthread_host_pid(thread));
#endif /* XENO_DEBUG(NUCLEUS) */

	if (xnarch_fault_pf_p(d))
		/*
		 * The page fault counter is not SMP-safe, but it's a
		 * simple indicator that something went wrong wrt
		 * memory locking anyway.
		 */
		xnstat_counter_inc(&thread->stat.pf);

	xnshadow_relax(xnarch_fault_notify(d), SIGDEBUG_MIGRATE_FAULT);

	return 0;
}

int ipipe_trap_hook(struct ipipe_trap_data *data)
{
	if (data->exception == IPIPE_TRAP_MAYDAY)
		return handle_mayday_event(data->regs);

	/*
	 * No migration is possible on behalf of the head domain, so
	 * the following access is safe.
	 */
	__this_cpu_ptr(&xnarch_percpu_machdata)->faults[data->exception]++;

	if (handle_exception(data))
		return EVENT_STOP;

	/*
	 * CAUTION: access faults must be propagated downstream
	 * whichever domain caused them, so that we don't spuriously
	 * raise a fatal error when some Linux fixup code is available
	 * to recover from the fault.
	 */
	return EVENT_PROPAGATE;
}

void xnshadow_grab_events(void)
{
	init_hostrt();
	ipipe_set_hooks(ipipe_root_domain, IPIPE_SYSCALL|IPIPE_KEVENT);
	ipipe_set_hooks(&xnarch_machdata.domain, IPIPE_SYSCALL|IPIPE_TRAP);
}

void xnshadow_release_events(void)
{
	ipipe_set_hooks(&xnarch_machdata.domain, 0);
	ipipe_set_hooks(ipipe_root_domain, 0);
}

int xnshadow_mount(void)
{
	unsigned int i, size;
	int ret;

	xnsynch_init(&yield_sync, XNSYNCH_FIFO, NULL);

	ret = xndebug_init();
	if (ret)
		return ret;

	/*
	 * Setup the mayday page early, before userland can mess with
	 * real-time ops.
	 */
	ret = mayday_init_page();
	if (ret) {
		xnshadow_cleanup();
		return ret;
	}

	size = sizeof(struct list_head) * PPD_HASH_SIZE;
	ppd_hash = kmalloc(size, GFP_KERNEL);
	if (ppd_hash == NULL) {
		xnshadow_cleanup();
		printk(XENO_ERR "cannot allocate PPD hash table\n");
		return -ENOMEM;
	}

	for (i = 0; i < PPD_HASH_SIZE; i++)
		INIT_LIST_HEAD(ppd_hash + i);

	user_muxid = xnshadow_register_personality(&user_personality);
	XENO_BUGON(NUCLEUS, user_muxid != 0);

	return 0;
}

void xnshadow_cleanup(void)
{
	if (user_muxid >= 0) {
		xnshadow_unregister_personality(user_muxid);
		user_muxid = -1;
	}

	if (ppd_hash) {
		kfree(ppd_hash);
		ppd_hash = NULL;
	}

	mayday_cleanup_page();

	xndebug_cleanup();

	xnsynch_destroy(&yield_sync);
}

/* Xenomai's generic personality. */
struct xnpersonality xenomai_personality = {
	.name = "xenomai",
	/* .magic = 0 */
};
EXPORT_SYMBOL_GPL(xenomai_personality);

/*@}*/

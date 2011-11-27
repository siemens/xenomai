/*!\file shadow.c
 * \brief Real-time shadow services.
 * \author Philippe Gerum
 *
 * Copyright (C) 2001-2008 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) 2004 The RTAI project <http://www.rtai.org>
 * Copyright (C) 2004 The HYADES project <http://www.hyades-itea.org>
 * Copyright (C) 2005 The Xenomai project <http://www.xenomai.org>
 * Copyright (C) 2006 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>
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
 * \ingroup shadow
 */

/*!
 * \ingroup nucleus
 * \defgroup shadow Real-time shadow services.
 *
 * Real-time shadow services.
 *
 *@{*/

#include <stdarg.h>
#include <linux/unistd.h>
#include <linux/wait.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/cred.h>
#include <linux/jhash.h>
#include <linux/file.h>
#include <asm/signal.h>
#include <nucleus/pod.h>
#include <nucleus/heap.h>
#include <nucleus/synch.h>
#include <nucleus/module.h>
#include <nucleus/shadow.h>
#include <nucleus/ppd.h>
#include <nucleus/trace.h>
#include <nucleus/stat.h>
#include <nucleus/sys_ppd.h>
#include <nucleus/vdso.h>
#include <nucleus/debug.h>
#include <asm/xenomai/features.h>
#include <asm/xenomai/syscall.h>
#include <asm/xenomai/bits/shadow.h>

static int xn_gid_arg = -1;
module_param_named(xenomai_gid, xn_gid_arg, int, 0644);
MODULE_PARM_DESC(xenomai_gid, "GID of the group with access to Xenomai services");

int nkthrptd;
EXPORT_SYMBOL_GPL(nkthrptd);
int nkerrptd;
EXPORT_SYMBOL_GPL(nkerrptd);
int nkmmptd;
EXPORT_SYMBOL_GPL(nkmmptd);

#define xnshadow_mmptd(t) ((t)->ptd[nkmmptd])
#define xnshadow_mm(t) ((struct mm_struct *)xnshadow_mmptd(t))

struct xnskin_slot {
	struct xnskin_props *props;
} skins[XENOMAI_SKINS_NR];

static int lostage_apc;

static struct __lostagerq {

	int in, out;

	struct {
#define LO_START_REQ  0
#define LO_WAKEUP_REQ 1
#define LO_SIGGRP_REQ 2
#define LO_SIGTHR_REQ 3
#define LO_UNMAP_REQ  4
		int type;
		struct task_struct *task;
		int arg;
#define LO_MAX_REQUESTS 64	/* Must be a ^2 */
	} req[LO_MAX_REQUESTS];

} lostagerq[XNARCH_NR_CPUS];

#define xnshadow_sig_mux(sig, arg) ((sig) | ((arg) << 8))
#define xnshadow_sig_demux(muxed, sig, arg) \
	do {				     \
		int _muxed = (muxed);	     \
		(sig) = _muxed & 0xff;	     \
		(arg) = _muxed >> 8;	     \
	} while (0)

static struct task_struct *switch_lock_owner[XNARCH_NR_CPUS];

static int nucleus_muxid = -1;

static struct semaphore completion_mutex;

static DEFINE_SEMAPHORE(registration_mutex);

static void *mayday_page;

static inline struct task_struct *get_switch_lock_owner(void)
{
	return switch_lock_owner[task_cpu(current)];
}

static inline void set_switch_lock_owner(struct task_struct *p)
{
	switch_lock_owner[task_cpu(p)] = p;
}

static xnqueue_t *ppd_hash;
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
static unsigned int ppd_lookup_inner(xnqueue_t **pq, xnshadow_ppd_t ** pholder,
				     xnshadow_ppd_key_t * pkey)
{
	union xnshadow_ppd_hkey key = { .mm = pkey->mm };
	unsigned bucket = jhash2(&key.val, sizeof(key) / sizeof(uint32_t), 0);
	xnshadow_ppd_t *ppd;
	xnholder_t *holder;

	*pq = &ppd_hash[bucket % PPD_HASH_SIZE];
	holder = getheadq(*pq);

	if (!holder) {
		*pholder = NULL;
		return 0;
	}

	do {
		ppd = link2ppd(holder);
		holder = nextq(*pq, holder);
	} while (holder &&
		 (ppd->key.mm < pkey->mm ||
		  (ppd->key.mm == pkey->mm && ppd->key.muxid > pkey->muxid)));

	if (ppd->key.mm == pkey->mm && ppd->key.muxid == pkey->muxid) {
		/* found it, return it. */
		*pholder = ppd;
		return 1;
	}

	/* not found, return successor for insertion. */
	if (ppd->key.mm < pkey->mm ||
	    (ppd->key.mm == pkey->mm && ppd->key.muxid > pkey->muxid))
		*pholder = holder ? link2ppd(holder) : NULL;
	else
		*pholder = ppd;

	return 0;
}

static int ppd_insert(xnshadow_ppd_t *holder)
{
	xnshadow_ppd_t *next;
	unsigned int found;
	xnqueue_t *q;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	found = ppd_lookup_inner(&q, &next, &holder->key);
	if (found) {
		xnlock_put_irqrestore(&nklock, s);
		return -EBUSY;
	}

	inith(&holder->link);
	if (next) {
		insertq(q, &next->link, &holder->link);
	} else {
		appendq(q, &holder->link);
	}

	xnlock_put_irqrestore(&nklock, s);

	return 0;
}

/* will be called by skin code, nklock locked irqs off. */
static xnshadow_ppd_t *ppd_lookup(unsigned muxid, struct mm_struct *mm)
{
	xnshadow_ppd_t *holder;
	xnshadow_ppd_key_t key;
	unsigned int found;
	xnqueue_t *q;

	key.muxid = muxid;
	key.mm = mm;

	found = ppd_lookup_inner(&q, &holder, &key);
	if (!found)
		return NULL;

	return holder;
}

static void ppd_remove(xnshadow_ppd_t * holder)
{
	unsigned int found;
	xnqueue_t *q;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	found = ppd_lookup_inner(&q, &holder, &holder->key);
	if (found)
		removeq(q, &holder->link);

	xnlock_put_irqrestore(&nklock, s);
}

static inline void ppd_remove_mm(struct mm_struct *mm,
				 void (*destructor) (xnshadow_ppd_t *))
{
	xnshadow_ppd_key_t key;
	xnshadow_ppd_t *ppd;
	xnholder_t *holder;
	xnqueue_t *q;
	spl_t s;

	key.muxid = ~0UL;
	key.mm = mm;
	xnlock_get_irqsave(&nklock, s);
	ppd_lookup_inner(&q, &ppd, &key);

	while (ppd && ppd->key.mm == mm) {
		holder = nextq(q, &ppd->link);
		removeq(q, &ppd->link);
		xnlock_put_irqrestore(&nklock, s);
		/*
		 * Releasing the nklock is safe here, if we assume
		 * that no insertion for the same mm will take place
		 * while we are running xnpod_remove_mm.
		 */
		destructor(ppd);
		ppd = holder ? link2ppd(holder) : NULL;
		xnlock_get_irqsave(&nklock, s);
	}

	xnlock_put_irqrestore(&nklock, s);
}

static void detach_ppd(xnshadow_ppd_t * ppd)
{
	unsigned int muxid = xnshadow_ppd_muxid(ppd);
	skins[muxid].props->eventcb(XNSHADOW_CLIENT_DETACH, ppd);
}

struct xnvdso *nkvdso;
EXPORT_SYMBOL_GPL(nkvdso);

/*
 * We re-use the global semaphore heap to provide a multi-purpose shared
 * memory area between Xenomai and Linux - for both kernel and userland
 */
void __init xnheap_init_vdso(void)
{
	nkvdso = (struct xnvdso *)
		xnheap_alloc(&__xnsys_global_ppd.sem_heap, sizeof(*nkvdso));
	if (nkvdso == NULL)
		xnpod_fatal("Xenomai: cannot allocate memory for xnvdso!\n");

	nkvdso->features = XNVDSO_FEATURES;
}

static inline void request_syscall_restart(xnthread_t *thread,
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

	xnshadow_relax(notify, SIGDEBUG_MIGRATE_SIGNAL);
}

static inline void set_linux_task_priority(struct task_struct *p, int prio)
{
	if (rthal_setsched_root(p, prio ? SCHED_FIFO : SCHED_NORMAL, prio) < 0)
		printk(KERN_WARNING
		       "Xenomai: invalid Linux priority level: %d, task=%s\n",
		       prio, p->comm);
}

static inline void lock_timers(void)
{
	xnarch_atomic_inc(&nkpod->timerlck);
	setbits(nkclock.status, XNTBLCK);
}

static inline void unlock_timers(void)
{
	if (xnarch_atomic_dec_and_test(&nkpod->timerlck))
		clrbits(nkclock.status, XNTBLCK);
}

static void lostage_handler(void *cookie)
{
	int cpu, reqnum, type, arg, sig, sigarg;
	struct __lostagerq *rq;
	struct task_struct *p;

	cpu = smp_processor_id();
	rq = &lostagerq[cpu];

	while ((reqnum = rq->out) != rq->in) {
		type = rq->req[reqnum].type;
		p = rq->req[reqnum].task;
		arg = rq->req[reqnum].arg;

		/* make sure we read the request before releasing its slot */
		barrier();

		rq->out = (reqnum + 1) & (LO_MAX_REQUESTS - 1);

		trace_mark(xn_nucleus, lostage_work,
			   "type %d comm %s pid %d",
			   type, p->comm, p->pid);

		switch (type) {
		case LO_UNMAP_REQ:
			/* fall through */
		case LO_WAKEUP_REQ:
			xnpod_schedule();
			/* fall through */
		case LO_START_REQ:
			wake_up_process(p);
			break;

		case LO_SIGTHR_REQ:
			xnshadow_sig_demux(arg, sig, sigarg);
			if (sig == SIGSHADOW || sig == SIGDEBUG) {
				siginfo_t si;
				memset(&si, '\0', sizeof(si));
				si.si_signo = sig;
				si.si_code = SI_QUEUE;
				si.si_int = sigarg;
				send_sig_info(sig, &si, p);
			} else
				send_sig(sig, p, 1);
			break;

		case LO_SIGGRP_REQ:
			kill_proc(p->pid, arg, 1);
			break;
		}
	}
}

static void schedule_linux_call(int type, struct task_struct *p, int arg)
{
	int cpu = rthal_processor_id(), reqnum;
	struct __lostagerq *rq;
	spl_t s;

	XENO_ASSERT(NUCLEUS, p,
		xnpod_fatal("schedule_linux_call() invoked "
			    "with NULL task pointer (req=%d, arg=%d)?!", type,
			    arg);
		);

	splhigh(s);

	rq = &lostagerq[cpu];
	reqnum = rq->in;
	rq->in = (reqnum + 1) & (LO_MAX_REQUESTS - 1);
	if (XENO_DEBUG(NUCLEUS) && rq->in == rq->out)
	    xnpod_fatal("lostage queue overflow on CPU %d! "
			"Increase LO_MAX_REQUESTS", cpu);
	rq->req[reqnum].type = type;
	rq->req[reqnum].task = p;
	rq->req[reqnum].arg = arg;

	__rthal_apc_schedule(lostage_apc);

	splexit(s);
}

static inline int normalize_priority(int prio)
{
	return prio < MAX_RT_PRIO ? prio : MAX_RT_PRIO - 1;
}

static int gatekeeper_thread(void *data)
{
	struct task_struct *this_task = current;
	int cpu = (long)data;
	struct xnsched *sched = xnpod_sched_slot(cpu);
	struct xnthread *target;
	cpumask_t cpumask;
	spl_t s;

	this_task->flags |= PF_NOFREEZE;
	sigfillset(&this_task->blocked);
	cpumask = cpumask_of_cpu(cpu);
	set_cpus_allowed(this_task, cpumask);
	set_linux_task_priority(this_task, MAX_RT_PRIO - 1);

	set_current_state(TASK_INTERRUPTIBLE);
	up(&sched->gksync);	/* Sync with xnshadow_mount(). */

	for (;;) {
		up(&sched->gksync); /* Make the request token available. */
		schedule();

		if (kthread_should_stop())
			break;

		/*
		 * Real-time shadow TCBs are always removed on behalf
		 * of the killed thread.
		 */
		target = sched->gktarget;

		/*
		 * In the very rare case where the requestor has been
		 * awaken by a signal before we have been able to
		 * process the pending request, just ignore the
		 * latter.
		 */
		if ((xnthread_user_task(target)->state & ~TASK_ATOMICSWITCH) == TASK_INTERRUPTIBLE) {
			xnlock_get_irqsave(&nklock, s);
#ifdef CONFIG_SMP
			/*
			 * If the task changed its CPU while in
			 * secondary mode, change the CPU of the
			 * underlying Xenomai shadow too. We do not
			 * migrate the thread timers here, it would
			 * not work. For a "full" migration comprising
			 * timers, using xnpod_migrate_thread is
			 * required.
			 */
			if (target->sched != sched)
				xnsched_migrate_passive(target, sched);
#endif /* CONFIG_SMP */
			xnpod_resume_thread(target, XNRELAX);
			xnlock_put_irqrestore(&nklock, s);
			xnpod_schedule();
		}
		set_current_state(TASK_INTERRUPTIBLE);
	}

	return 0;
}

/*!
 * @internal
 * \fn int xnshadow_harden(void);
 * \brief Migrate a Linux task to the Xenomai domain.
 *
 * This service causes the transition of "current" from the Linux
 * domain to Xenomai. This is obtained by asking the gatekeeper to
 * resume the shadow mated with "current" then triggering the
 * rescheduling procedure in the Xenomai domain. The shadow will
 * resume in the Xenomai domain as returning from schedule().
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - User-space thread operating in secondary (i.e. relaxed) mode.
 *
 * Rescheduling: always.
 */

int xnshadow_harden(void)
{
	struct task_struct *this_task = current;
	struct xnthread *thread;
	struct xnsched *sched;
	int cpu, err;

redo:
	thread = xnshadow_thread(this_task);
	if (!thread)
		return -EPERM;

	cpu = task_cpu(this_task);
	sched = xnpod_sched_slot(cpu);

	/* Grab the request token. */
	if (down_interruptible(&sched->gksync)) {
		err = -ERESTARTSYS;
		goto failed;
	}

	if (thread->u_mode)
		*(thread->u_mode) = thread->state & ~XNRELAX;

	preempt_disable();

	/*
	 * Assume that we might have been migrated while waiting for
	 * the token. Redo acquisition in such a case, so that we
	 * don't mistakenly send the request to the wrong
	 * gatekeeper.
	 */
	if (cpu != task_cpu(this_task)) {
		preempt_enable();
		up(&sched->gksync);
		goto redo;
	}

	/*
	 * Set up the request to move "current" from the Linux domain
	 * to the Xenomai domain. This will cause the shadow thread to
	 * resume using the register state of the current Linux
	 * task. For this to happen, we set up the migration data,
	 * prepare to suspend the current task, wake up the gatekeeper
	 * which will perform the actual transition, then schedule
	 * out.
	 */

	trace_mark(xn_nucleus, shadow_gohard,
		   "thread %p thread_name %s comm %s",
		   thread, xnthread_name(thread), this_task->comm);

	sched->gktarget = thread;
	xnthread_set_info(thread, XNATOMIC);
	set_current_state(TASK_INTERRUPTIBLE | TASK_ATOMICSWITCH);

	wake_up_process(sched->gatekeeper);

	schedule();
	xnthread_clear_info(thread, XNATOMIC);

	/*
	 * Rare case: we might have received a signal before entering
	 * schedule() and returned early from it. Since TASK_UNINTERRUPTIBLE
	 * is unavailable to us without wrecking the runqueue's count of
	 * uniniterruptible tasks, we just notice the issue and gracefully
	 * fail; the caller will have to process this signal anyway.
	 */
	if (rthal_current_domain == rthal_root_domain) {
		if (XENO_DEBUG(NUCLEUS) && (!signal_pending(this_task)
		    || this_task->state != TASK_RUNNING))
			xnpod_fatal
			    ("xnshadow_harden() failed for thread %s[%d]",
			     thread->name, xnthread_user_pid(thread));

		/*
		 * Synchronize with the chosen gatekeeper so that it no longer
		 * holds any reference to this thread and does not develop the
		 * idea to resume it for the Xenomai domain if, later on, we
		 * may happen to reenter TASK_INTERRUPTIBLE state.
		 */
		down(&sched->gksync);
		up(&sched->gksync);

		return -ERESTARTSYS;
	}

	/* "current" is now running into the Xenomai domain. */
	sched = xnsched_finish_unlocked_switch(thread->sched);

	xnsched_finalize_zombie(sched);

#ifdef CONFIG_XENO_HW_FPU
	xnpod_switch_fpu(sched);
#endif /* CONFIG_XENO_HW_FPU */

	xnarch_schedule_tail(this_task);

	if (xnthread_signaled_p(thread))
		xnpod_dispatch_signals();

	xnlock_clear_irqon(&nklock);

	trace_mark(xn_nucleus, shadow_hardened, "thread %p thread_name %s",
		   thread, xnthread_name(thread));

	/*
	 * Recheck pending signals once again. As we block task wakeups during
	 * the migration and do_sigwake_event ignores signals until XNRELAX is
	 * left, any signal between entering TASK_ATOMICSWITCH and starting
	 * the migration in the gatekeeker thread is just silently queued up
	 * to here.
	 */
	if (signal_pending(this_task)) {
		xnshadow_relax(!xnthread_test_state(thread, XNDEBUG),
			       SIGDEBUG_MIGRATE_SIGNAL);
		return -ERESTARTSYS;
	}

	xnsched_resched_after_unlocked_switch();

	return 0;

      failed:
	if (thread->u_mode)
		*(thread->u_mode) = thread->state;
	return err;
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
 * Linux task will resume on return from xnpod_suspend_thread() on
 * behalf of the root thread.
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
	xnthread_t *thread = xnpod_current_thread();
	siginfo_t si;
	int prio;

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
	 * sequence, /first/ make sure to:
	 *
	 * - read commit #d3242401b8
	 *
	 * - check the special handling of XNRELAX in
	 * xnpod_suspend_thread() when switching out the current
	 * thread, not to break basic assumptions we do there.
	 *
	 * We disable interrupts during the migration sequence, but
	 * xnpod_suspend_thread() has an interrupts-on section built in.
	 */
	splmax();
	schedule_linux_call(LO_WAKEUP_REQ, current, 0);

	/*
	 * Task nklock to synchronize the Linux task state manipulation with
	 * do_sigwake_event. nklock will be released by xnpod_suspend_thread.
	 */
	xnlock_get(&nklock);
	clear_task_nowakeup(current);
	xnpod_suspend_thread(thread, XNRELAX, XN_INFINITE, XN_RELATIVE, NULL);

	splnone();
	if (XENO_DEBUG(NUCLEUS) && rthal_current_domain != rthal_root_domain)
		xnpod_fatal("xnshadow_relax() failed for thread %s[%d]",
			    thread->name, xnthread_user_pid(thread));

	prio = normalize_priority(xnthread_current_priority(thread));
	rthal_reenter_root(get_switch_lock_owner(),
			   prio ? SCHED_FIFO : SCHED_NORMAL, prio);

	xnstat_counter_inc(&thread->stat.ssw);	/* Account for secondary mode switch. */

	if (notify) {
		xndebug_notify_relax(thread, reason);
		if (xnthread_test_state(thread, XNTRAPSW)) {
			/* Help debugging spurious relaxes. */
			memset(&si, 0, sizeof(si));
			si.si_signo = SIGDEBUG;
			si.si_code = SI_QUEUE;
			si.si_int = reason;
			send_sig_info(SIGDEBUG, &si, current);
		}
		xnsynch_detect_claimed_relax(thread);
	}

	if (xnthread_test_info(thread, XNPRIOSET)) {
		xnthread_clear_info(thread, XNPRIOSET);
		xnshadow_send_sig(thread, SIGSHADOW,
				  sigshadow_int(SIGSHADOW_ACTION_RENICE, prio),
				  1);
	}

#ifdef CONFIG_SMP
	/* If the shadow thread changed its CPU affinity while in
	   primary mode, reset the CPU affinity of its Linux
	   counter-part when returning to secondary mode. [Actually,
	   there is no service changing the CPU affinity from primary
	   mode available from the nucleus --rpm]. */
	if (xnthread_test_info(thread, XNAFFSET)) {
		xnthread_clear_info(thread, XNAFFSET);
		set_cpus_allowed(current, xnthread_affinity(thread));
	}
#endif /* CONFIG_SMP */

	/* "current" is now running into the Linux domain on behalf of the
	   root thread. */

	if (thread->u_mode)
		*(thread->u_mode) = thread->state;

	trace_mark(xn_nucleus, shadow_relaxed,
		  "thread %p thread_name %s comm %s",
		  thread, xnthread_name(thread), current->comm);
}
EXPORT_SYMBOL_GPL(xnshadow_relax);

int xnshadow_force_wakeup(struct xnthread *thread) /* nklock locked, irqs off */
{
	int ret = 0;

	if (xnthread_test_info(thread, XNKICKED))
		return 1;

	/*
	 * Tricky case: a ready thread does not actually run, but
	 * nevertheless waits for the CPU in primary mode, so we have
	 * to make sure that it will be notified of the pending break
	 * condition as soon as it enters xnpod_suspend_thread() from
	 * a blocking Xenomai syscall.
	 */
	if (xnthread_test_state(thread, XNREADY)) {
		xnthread_set_info(thread, XNKICKED);
		xnsched_set_resched(thread->sched);
		return 0;
	}

	if (xnpod_unblock_thread(thread)) {
		xnthread_set_info(thread, XNKICKED);
		ret = 1;
	}

	if (xnthread_test_state(thread, XNSUSP|XNHELD)) {
		xnpod_resume_thread(thread, XNSUSP|XNHELD);
		xnthread_set_info(thread, XNKICKED|XNBREAK);
	}

	/*
	 * Check whether a thread was started and later stopped, in
	 * which case it is blocked by the nucleus, and we have to
	 * wake it up. The kernel will wake up unstarted threads
	 * blocked in xnshadow_sys_barrier() as needed.
	 */
	if (xnthread_test_state(thread, XNDORMANT|XNSTARTED) == (XNDORMANT|XNSTARTED)) {
		xnpod_resume_thread(thread, XNDORMANT);
		xnthread_set_info(thread, XNKICKED|XNBREAK);
	}

	return ret;
}

void xnshadow_kick(struct xnthread *thread) /* nklock locked, irqs off */
{
	struct task_struct *p = xnthread_archtcb(thread)->user_task;

	/* Thread is already relaxed -- nop. */
	if (xnthread_test_state(thread, XNRELAX))
		return;

	/*
	 * First, try to kick the thread out of any blocking syscall
	 * Xenomai-wise. If that succeeded, then the thread will relax
	 * on its return path to user-space.
	 */
	if (xnshadow_force_wakeup(thread))
		return;

	/*
	 * If that did not work out because the thread was not blocked
	 * (i.e. XNPEND/XNDELAY) in a syscall, then force a mayday
	 * trap. Note that we don't want to send that thread any linux
	 * signal, we only want to force it to switch to secondary
	 * mode.
	 *
	 * It could happen that a thread is relaxed on a syscall
	 * return path after it was resumed from self-suspension
	 * (e.g. XNSUSP) then also forced to run a mayday trap right
	 * after: this is still correct, at worst we would get a
	 * useless mayday syscall leading to a no-op, no big deal.
	 */
	xnthread_set_info(thread, XNKICKED);

	/*
	 * No need to run a mayday trap if the current thread kicks
	 * itself out of primary mode: it will relax on its way back
	 * to userland via the current syscall epilogue. Otherwise, we
	 * want that thread to enter the mayday trap, to call us back
	 * immediately for relaxing.
	 */
	if (thread != xnpod_current_thread())
		xnarch_call_mayday(p);
}
EXPORT_SYMBOL_GPL(xnshadow_kick);

void xnshadow_demote(struct xnthread *thread) /* nklock locked, irqs off */
{
	/*
	 * First we kick the thread out of primary mode, and have it
	 * resume execution immediately over the regular linux
	 * context.
	 */
	xnshadow_kick(thread);
	/*
	 * Then we send it a renice action signal to demote it from
	 * SCHED_FIFO to SCHED_OTHER. In effect, we turned that thread
	 * into a non real-time Xenomai shadow, which still has access
	 * to Xenomai resources, but won't compete anymore for
	 * real-time scheduling.
	 */
	xnshadow_send_sig(thread, SIGSHADOW,
			  sigshadow_int(SIGSHADOW_ACTION_RENICE, 0),
			  1);
}
EXPORT_SYMBOL_GPL(xnshadow_demote);

void xnshadow_exit(void)
{
	rthal_reenter_root(get_switch_lock_owner(),
			   current->rt_priority ? SCHED_FIFO : SCHED_NORMAL,
			   current->rt_priority);
	do_exit(0);
}

/*!
 * \fn int xnshadow_map(xnthread_t *thread,
 *                      xncompletion_t __user *u_completion,
 *                      unsigned long __user *u_mode_offset)
 * @internal
 * \brief Create a shadow thread context.
 *
 * This call maps a nucleus thread to the "current" Linux task.  The
 * priority and scheduling class of the underlying Linux task are not
 * affected; it is assumed that the interface library did set them
 * appropriately before issuing the shadow mapping request.
 *
 * @param thread The descriptor address of the new shadow thread to be
 * mapped to "current". This descriptor must have been previously
 * initialized by a call to xnpod_init_thread().
 *
 * @param u_completion is the address of an optional completion
 * descriptor aimed at synchronizing our parent thread with us. If
 * non-NULL, the information xnshadow_map() will store into the
 * completion block will be later used to wake up the parent thread
 * when the current shadow has been initialized. In the latter case,
 * the new shadow thread is left in a dormant state (XNDORMANT) after
 * its creation, leading to the suspension of "current" in the Linux
 * domain, only processing signals. Otherwise, the shadow thread is
 * immediately started and "current" immediately resumes in the Xenomai
 * domain from this service.
 *
 * @param: u_mode_offset is the address of a user space address where
 * we will store the offset of the "u_mode" thread variable in the
 * process shared heap. This thread variable reflects the current
 * thread mode (primary or secondary). The nucleus will try to update
 * the variable before switching to secondary  or after switching from
 * primary mode.
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
 * - -EINVAL is returned if the thread control block does not bear the
 * XNSHADOW bit.
 *
 * - -EBUSY is returned if either the current Linux task or the
 * associated shadow thread is already involved in a shadow mapping.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Regular user-space process.
 *
 * Rescheduling: always.
 *
 */

int xnshadow_map(xnthread_t *thread, xncompletion_t __user *u_completion,
		 unsigned long __user *u_mode_offset)
{
	struct xnthread_start_attr attr;
	xnarch_cpumask_t affinity;
	struct xnsys_ppd *sys_ppd;
	unsigned long *u_mode;
	xnheap_t *sem_heap;
	spl_t s;
	int ret;

	if (!xnthread_test_state(thread, XNSHADOW))
		return -EINVAL;

	if (xnshadow_thread(current) || xnthread_test_state(thread, XNMAPPED))
		return -EBUSY;

	if (!access_wok(u_mode_offset, sizeof(*u_mode_offset)))
		return -EFAULT;

#ifdef CONFIG_MMU
	if (!(current->mm->def_flags & VM_LOCKED)) {
		siginfo_t si;

		memset(&si, 0, sizeof(si));
		si.si_signo = SIGDEBUG;
		si.si_code = SI_QUEUE;
		si.si_int = SIGDEBUG_NOMLOCK;
		send_sig_info(SIGDEBUG, &si, current);
	} else
		if ((ret = rthal_disable_ondemand_mappings(current)))
			return ret;
#endif /* CONFIG_MMU */

	xnlock_get_irqsave(&nklock, s);
	sys_ppd = xnsys_ppd_get(0);
	xnlock_put_irqrestore(&nklock, s);

	sem_heap = &sys_ppd->sem_heap;
	u_mode = xnheap_alloc(sem_heap, sizeof(*u_mode));
	if (!u_mode)
		return -ENOMEM;

	/* Restrict affinity to a single CPU of nkaffinity & current set. */
	xnarch_cpus_and(affinity, current->cpus_allowed, nkaffinity);
	affinity = xnarch_cpumask_of_cpu(xnarch_first_cpu(affinity));
	set_cpus_allowed(current, affinity);

	trace_mark(xn_nucleus, shadow_map,
		   "thread %p thread_name %s pid %d priority %d",
		   thread, xnthread_name(thread), current->pid,
		   xnthread_base_priority(thread));

	xnarch_init_shadow_tcb(xnthread_archtcb(thread), thread,
			       xnthread_name(thread));

	thread->u_mode = u_mode;
	__xn_put_user(xnheap_mapped_offset(sem_heap, u_mode), u_mode_offset);

	xnthread_set_state(thread, XNMAPPED);
	xnpod_suspend_thread(thread, XNRELAX, XN_INFINITE, XN_RELATIVE, NULL);
	xndebug_shadow_init(thread);

	/*
	 * Switch on propagation of normal kernel events for the bound
	 * task. This is basically a per-task event filter which
	 * restricts event notifications (e.g. syscalls) to Linux
	 * tasks bearing a specific flag, so that we don't uselessly
	 * intercept those events when they happen to be caused by
	 * plain (i.e. non-Xenomai) Linux tasks.
	 *
	 * CAUTION: we arm the notification callback only when the
	 * shadow TCB is consistent, so that we won't trigger false
	 * positive in debug code from do_schedule_event() and
	 * friends.
	 */
	xnshadow_thrptd(current) = thread;
	xnshadow_mmptd(current) = current->mm;
	xnarch_atomic_inc(&sys_ppd->refcnt);

	rthal_enable_notifier(current);

	if (xnthread_base_priority(thread) == 0 &&
	    current->policy == SCHED_NORMAL)
		/* Non real-time shadow. */
		xnthread_set_state(thread, XNOTHER);

	if (u_completion) {
		/*
		 * Send the renice signal if we are not migrating so
		 * that user space will immediately align Linux sched
		 * policy and prio.
		 */
		xnshadow_renice(thread);
		xnshadow_signal_completion(u_completion, 0);
		return 0;
	}

	/* Nobody waits for us, so we may start the shadow immediately. */
	attr.mode = 0;
	attr.imask = 0;
	attr.affinity = affinity;
	attr.entry = NULL;
	attr.cookie = NULL;
	ret = xnpod_start_thread(thread, &attr);
	if (ret)
		return ret;

	if (thread->u_mode)
		*(thread->u_mode) = thread->state;

	ret = xnshadow_harden();

	/*
	 * Ensure that user space will receive the proper Linux task policy
	 * and prio on next switch to secondary mode.
	 */
	xnthread_set_info(thread, XNPRIOSET);

	xnarch_trace_pid(xnthread_user_pid(thread),
			 xnthread_current_priority(thread));

	return ret;
}
EXPORT_SYMBOL_GPL(xnshadow_map);

void xnshadow_unmap(xnthread_t *thread)
{
	struct xnsys_ppd *sys_ppd;
	struct task_struct *p;

	if (XENO_DEBUG(NUCLEUS) &&
	    !testbits(xnpod_current_sched()->status, XNKCOUT))
		xnpod_fatal("xnshadow_unmap() called from invalid context");

	p = xnthread_archtcb(thread)->user_task;
	xnthread_clear_state(thread, XNMAPPED);

	sys_ppd = xnsys_ppd_get(0);
	xnheap_free(&sys_ppd->sem_heap, thread->u_mode);
	thread->u_mode = NULL;

	xnarch_atomic_dec(&sys_ppd->refcnt);

	trace_mark(xn_nucleus, shadow_unmap,
		   "thread %p thread_name %s pid %d",
		   thread, xnthread_name(thread), p ? p->pid : -1);

	if (!p)
		return;

	XENO_ASSERT(NUCLEUS, p == current,
		    xnpod_fatal("%s invoked for a non-current task (t=%s/p=%s)",
				__FUNCTION__, thread->name, p->comm);
		);

	xnshadow_thrptd(p) = NULL;

	schedule_linux_call(LO_UNMAP_REQ, p, xnthread_get_magic(thread));
}
EXPORT_SYMBOL_GPL(xnshadow_unmap);

void xnshadow_start(struct xnthread *thread)
{
	struct task_struct *p = xnthread_archtcb(thread)->user_task;

	trace_mark(xn_nucleus, shadow_start, "thread %p thread_name %s",
		   thread, xnthread_name(thread));
	xnpod_resume_thread(thread, XNDORMANT);

	if (p->state == TASK_INTERRUPTIBLE)
		/* Wakeup the Linux mate waiting on the barrier. */
		schedule_linux_call(LO_START_REQ, p, 0);
}
EXPORT_SYMBOL_GPL(xnshadow_start);

/* Called with nklock locked, Xenomai interrupts off. */
void xnshadow_renice(struct xnthread *thread)
{
	/*
	 * We need to bound the priority values in the
	 * [1..MAX_RT_PRIO-1] range, since the Xenomai priority scale
	 * is a superset of the Linux priority scale.
	 */
	int prio = normalize_priority(thread->cprio);

	xnshadow_send_sig(thread, SIGSHADOW,
			  sigshadow_int(SIGSHADOW_ACTION_RENICE, prio), 1);
}

void xnshadow_suspend(struct xnthread *thread)
{
	/* Called with nklock locked, Xenomai interrupts off. */
	xnshadow_send_sig(thread, SIGSHADOW, SIGSHADOW_ACTION_HARDEN, 1);
}
EXPORT_SYMBOL_GPL(xnshadow_suspend);

static int xnshadow_sys_migrate(int domain)
{
	struct xnthread *thread = xnshadow_thread(current);

	if (rthal_current_domain == rthal_root_domain) {
		if (domain == XENOMAI_XENO_DOMAIN) {
			if (thread == NULL)
				return -EPERM;
			/*
			 * Paranoid: a corner case where the
			 * user-space side fiddles with SIGSHADOW
			 * while the target thread is still waiting to
			 * be started (whether it was never started or
			 * it was stopped).
			 */
			if (xnthread_test_state(thread, XNDORMANT))
				return 0;

			return xnshadow_harden() ? : 1;
		}
		return 0;
	}

	/* rthal_current_domain != rthal_root_domain */
	if (domain == XENOMAI_LINUX_DOMAIN) {
		xnshadow_relax(0, 0);
		return 1;
	}

	return 0;
}

static void stringify_feature_set(u_long fset, char *buf, int size)
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
	return xnarch_remap_vm_page(vma, vma->vm_start,
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
	down_write(&p->mm->mmap_sem);
	u_addr = do_mmap(filp, 0, PAGE_SIZE, PROT_EXEC|PROT_READ, MAP_SHARED, 0);
	up_write(&p->mm->mmap_sem);
	filp->f_op = (typeof(filp->f_op))old_fops;
	filp_close(filp, p->files);

	return IS_ERR_VALUE(u_addr) ? 0UL : u_addr;
}

/* nklock locked, irqs off */
void xnshadow_call_mayday(struct xnthread *thread, int sigtype)
{
	struct task_struct *p = xnthread_archtcb(thread)->user_task;
	xnthread_set_info(thread, XNKICKED);
	xnshadow_send_sig(thread, SIGDEBUG, sigtype, 1);
	xnarch_call_mayday(p);
}
EXPORT_SYMBOL_GPL(xnshadow_call_mayday);

static int xnshadow_sys_mayday(void)
{
	struct xnthread *cur;

	cur = xnshadow_thread(current);
	if (likely(cur)) {
		/*
		 * If the thread was kicked by the watchdog, this
		 * syscall we have just forced on it via the mayday
		 * escape will cause it to relax. See
		 * do_hisyscall_event().
		 */
		xnarch_fixup_mayday(xnthread_archtcb(cur), cur->regs);
		return 0;
	}

	printk(KERN_WARNING
	       "Xenomai: MAYDAY received from invalid context %s[%d]\n",
	       current->comm, current->pid);

	return -EPERM;
}

static inline int mayday_init_page(void)
{
	mayday_page = vmalloc(PAGE_SIZE);
	if (mayday_page == NULL) {
		printk(KERN_ERR "Xenomai: can't alloc MAYDAY page\n");
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

static inline void do_mayday_event(struct pt_regs *regs)
{
	struct xnthread *thread = xnshadow_thread(current);
	struct xnarchtcb *tcb = xnthread_archtcb(thread);
	struct xnsys_ppd *sys_ppd;

	/* We enter the event handler with hw IRQs off. */
	xnlock_get(&nklock);
	sys_ppd = xnsys_ppd_get(0);
	xnlock_put(&nklock);
	XENO_BUGON(NUCLEUS, sys_ppd == NULL);

	xnarch_handle_mayday(tcb, regs, sys_ppd->mayday_addr);
}

RTHAL_DECLARE_MAYDAY_EVENT(mayday_event);

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
			     xnsysinfo_t __user *u_breq)
{
	xnshadow_ppd_t *ppd = NULL, *sys_ppd = NULL;
	unsigned long featreq, featmis;
	struct xnskin_slot *sslt;
	int muxid, abirev, err;
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
	    (xn_gid_arg == -1 || !in_group_p(xn_gid_arg)))
		return -EPERM;

	/* Raise capabilities for the caller in case they are lacking yet. */
	raise_cap(CAP_SYS_NICE);
	raise_cap(CAP_IPC_LOCK);
	raise_cap(CAP_SYS_RAWIO);

	xnlock_get_irqsave(&nklock, s);

	for (muxid = 1; muxid < XENOMAI_SKINS_NR; muxid++) {
		sslt = skins + muxid;
		if (sslt->props && sslt->props->magic == magic)
			goto do_bind;
	}

	xnlock_put_irqrestore(&nklock, s);

	return -ESRCH;

      do_bind:

	xnlock_put_irqrestore(&nklock, s);

	/*
	 * Since the pod might be created by the event callback and not
	 * earlier than that, do not refer to nkpod until the latter had a
	 * chance to call xnpod_init().
	 */
	xnlock_get_irqsave(&nklock, s);
	sys_ppd = ppd_lookup(0, current->mm);
	xnlock_put_irqrestore(&nklock, s);

	if (sys_ppd)
		goto muxid_eventcb;

	sys_ppd = skins[0].props->eventcb(XNSHADOW_CLIENT_ATTACH, current);
	if (IS_ERR(sys_ppd)) {
		err = PTR_ERR(sys_ppd);
		goto fail;
	}

	if (sys_ppd == NULL)
		goto muxid_eventcb;

	sys_ppd->key.muxid = 0;
	sys_ppd->key.mm = current->mm;
	if (ppd_insert(sys_ppd) == -EBUSY) {
		/* In case of concurrent binding (which can not happen with
		   Xenomai libraries), detach right away the second ppd. */
		skins[0].props->eventcb(XNSHADOW_CLIENT_DETACH, sys_ppd);
		sys_ppd = NULL;
	}

muxid_eventcb:

	if (!sslt->props->eventcb)
		goto eventcb_done;

	xnlock_get_irqsave(&nklock, s);
	ppd = ppd_lookup(muxid, current->mm);
	xnlock_put_irqrestore(&nklock, s);

	/* protect from the same process binding several times. */
	if (ppd)
		goto eventcb_done;

	ppd = sslt->props->eventcb(XNSHADOW_CLIENT_ATTACH, current);
	if (IS_ERR(ppd)) {
		err = PTR_ERR(ppd);
		goto fail_destroy_sys_ppd;
	}

	if (ppd == NULL)
		goto eventcb_done;

	ppd->key.muxid = muxid;
	ppd->key.mm = current->mm;

	if (ppd_insert(ppd) == -EBUSY) {
		/*
		 * In case of concurrent binding (which can not happen
		 * with Xenomai libraries), detach right away the
		 * second ppd.
		 */
		sslt->props->eventcb(XNSHADOW_CLIENT_DETACH, ppd);
		ppd = NULL;
	}

eventcb_done:

	if (!xnpod_active_p()) {
		/* Ok mate, but you really ought to call xnpod_init()
		   at some point if you want me to be of some help
		   here... */
		if (sslt->props->eventcb && ppd) {
			ppd_remove(ppd);
			sslt->props->eventcb(XNSHADOW_CLIENT_DETACH, ppd);
		}

		err = -ENOSYS;

	fail_destroy_sys_ppd:
		if (sys_ppd) {
			ppd_remove(sys_ppd);
			skins[0].props->eventcb(XNSHADOW_CLIENT_DETACH, sys_ppd);
		}
	      fail:
		return err;
	}

	return muxid;
}

static int xnshadow_sys_info(int muxid, xnsysinfo_t __user *u_info)
{
	xnsysinfo_t info;

	if (muxid < 0 || muxid > XENOMAI_SKINS_NR ||
	    skins[muxid].props == NULL)
		return -EINVAL;

	info.clockfreq = xnarch_get_clock_freq();
	info.vdso = xnheap_mapped_offset(&xnsys_ppd_get(1)->sem_heap,
					 nkvdso);

	return __xn_safe_copy_to_user(u_info, &info, sizeof(info));
}

#define completion_value_ok ((1UL << (BITS_PER_LONG-1))-1)

void xnshadow_signal_completion(xncompletion_t __user *u_completion, int err)
{
	xncompletion_t completion;
	struct task_struct *p;
	int discarded;
	pid_t pid;

	/* Hold a mutex to avoid missing a wakeup signal. */
	down(&completion_mutex);

	if (__xn_safe_copy_from_user(&completion, u_completion, sizeof(completion))) {
		up(&completion_mutex);
		return;
	}

	/* Poor man's semaphore V. */
	completion.syncflag = err ? : completion_value_ok;
	discarded = __xn_safe_copy_to_user(u_completion, &completion, sizeof(completion));
	pid = completion.pid;

	up(&completion_mutex);

	if (pid == -1)
		return;

	read_lock(&tasklist_lock);

	p = wrap_find_task_by_pid(completion.pid);

	if (p)
		wake_up_process(p);

	read_unlock(&tasklist_lock);
}
EXPORT_SYMBOL_GPL(xnshadow_signal_completion);

static int xnshadow_sys_completion(xncompletion_t __user *u_completion)
{
	xncompletion_t completion;
	int discarded;

	for (;;) {		/* Poor man's semaphore P. */
		down(&completion_mutex);

		if (__xn_safe_copy_from_user(&completion, u_completion, sizeof(completion))) {
			completion.syncflag = -EFAULT;
			break;
		}

		if (completion.syncflag)
			break;

		completion.pid = current->pid;

		if (__xn_safe_copy_to_user(u_completion, &completion, sizeof(completion))) {
			completion.syncflag = -EFAULT;
			break;
		}

		set_current_state(TASK_INTERRUPTIBLE);

		up(&completion_mutex);

		schedule();

		if (signal_pending(current)) {
			completion.pid = -1;
			discarded = __xn_safe_copy_to_user(u_completion, &completion, sizeof(completion));
			return -ERESTARTSYS;
		}
	}

	up(&completion_mutex);

	return completion.syncflag == completion_value_ok ? 0 : (int)completion.syncflag;
}

static int xnshadow_sys_barrier(void __user **u_entry,
				void __user **u_cookie)
{
	xnthread_t *thread = xnshadow_thread(current);
	spl_t s;

	if (thread == NULL)
		return -EPERM;

	xnlock_get_irqsave(&nklock, s);

	if (xnthread_test_state(thread, XNSTARTED)) {
		/* Already done -- no op. */
		xnlock_put_irqrestore(&nklock, s);
		goto release_task;
	}

	/* We must enter this call on behalf of the Linux domain. */
	set_current_state(TASK_INTERRUPTIBLE);
	xnlock_put_irqrestore(&nklock, s);

	schedule();

	if (signal_pending(current))
		return -ERESTARTSYS;

	if (!xnthread_test_state(thread, XNSTARTED))	/* Not really paranoid. */
		return -EPERM;

release_task:

	if (u_entry &&
	    __xn_safe_copy_to_user(u_entry, &thread->entry,
				   sizeof(thread->entry)))
		return -EFAULT;

	if (u_cookie &&
	    __xn_safe_copy_to_user(u_cookie, &thread->cookie,
				   sizeof(thread->cookie)))
		return -EFAULT;

	return xnshadow_harden();
}

static int xnshadow_sys_trace(int op, unsigned long a1,
			      unsigned long a2, unsigned long a3)
{
	int err = -ENOSYS;

	switch (op) {
	case __xntrace_op_max_begin:
		err = xnarch_trace_max_begin(a1);
		break;

	case __xntrace_op_max_end:
		err = xnarch_trace_max_end(a1);
		break;

	case __xntrace_op_max_reset:
		err = xnarch_trace_max_reset();
		break;

	case __xntrace_op_user_start:
		err = xnarch_trace_user_start();
		break;

	case __xntrace_op_user_stop:
		err = xnarch_trace_user_stop(a1);
		break;

	case __xntrace_op_user_freeze:
		err = xnarch_trace_user_freeze(a1, a2);
		break;

	case __xntrace_op_special:
		err = xnarch_trace_special(a1 & 0xFF, a2);
		break;

	case __xntrace_op_special_u64:
		err = xnarch_trace_special_u64(a1 & 0xFF,
					       (((u64) a2) << 32) | a3);
		break;
	}
	return err;
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

#if CONFIG_XENO_OPT_SYS_STACKPOOLSZ > 0
	case XNHEAP_SYS_STACKPOOL:
		heap = &kstacks;
		break;
#endif

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
	xnthread_t *cur = xnshadow_thread(current);

	if (cur == NULL)
		return -EPERM;

	return __xn_safe_copy_to_user(u_handle, &xnthread_handle(cur),
				      sizeof(*u_handle));
}

static int xnshadow_sys_current_info(xnthread_info_t __user *u_info)
{
	xnthread_t *cur = xnshadow_thread(current);
	xnticks_t raw_exectime;
	xnthread_info_t info;
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
	info.exectime = xnarch_tsc_to_ns(raw_exectime);
	info.modeswitches = xnstat_counter_get(&cur->stat.ssw);
	info.ctxswitches = xnstat_counter_get(&cur->stat.csw);
	info.pagefaults = xnstat_counter_get(&cur->stat.pf);
	info.syscalls = xnstat_counter_get(&cur->stat.xsc);
	strcpy(info.name, xnthread_name(cur));

	return __xn_safe_copy_to_user(u_info, &info, sizeof(*u_info));
}

static int xnshadow_sys_backtrace(int nr, unsigned long *u_backtrace,
				  int reason)
{
	xndebug_trace_relax(nr, u_backtrace, reason);
	return 0;
}

static struct xnsysent __systab[] = {
	SKINCALL_DEF(__xn_sys_migrate, xnshadow_sys_migrate, current),
	SKINCALL_DEF(__xn_sys_arch, xnarch_local_syscall, any),
	SKINCALL_DEF(__xn_sys_bind, xnshadow_sys_bind, lostage),
	SKINCALL_DEF(__xn_sys_info, xnshadow_sys_info, lostage),
	SKINCALL_DEF(__xn_sys_completion, xnshadow_sys_completion, lostage),
	SKINCALL_DEF(__xn_sys_barrier, xnshadow_sys_barrier, lostage),
	SKINCALL_DEF(__xn_sys_trace, xnshadow_sys_trace, any),
	SKINCALL_DEF(__xn_sys_heap_info, xnshadow_sys_heap_info, lostage),
	SKINCALL_DEF(__xn_sys_current, xnshadow_sys_current, any),
	SKINCALL_DEF(__xn_sys_current_info, xnshadow_sys_current_info, shadow),
	SKINCALL_DEF(__xn_sys_mayday, xnshadow_sys_mayday, oneway),
	SKINCALL_DEF(__xn_sys_backtrace, xnshadow_sys_backtrace, current),
};

static void post_ppd_release(struct xnheap *h)
{
	struct xnsys_ppd *p = container_of(h, struct xnsys_ppd, sem_heap);
	xnarch_free_host_mem(p, sizeof(*p));
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

static void *xnshadow_sys_event(int event, void *data)
{
	struct xnsys_ppd *p;
	char *exe_path;
	int ret;

	switch(event) {
	case XNSHADOW_CLIENT_ATTACH:
		p = xnarch_alloc_host_mem(sizeof(*p));
		if (p == NULL)
			return ERR_PTR(-ENOMEM);

		ret = xnheap_init_mapped(&p->sem_heap,
					 CONFIG_XENO_OPT_SEM_HEAPSZ * 1024,
					 XNARCH_SHARED_HEAP_FLAGS);
		if (ret) {
			xnarch_free_host_mem(p, sizeof(*p));
			return ERR_PTR(ret);
		}

		xnheap_set_label(&p->sem_heap,
				 "private sem heap [%d]", current->pid);

		p->mayday_addr = map_mayday_page(current);
		if (p->mayday_addr == 0) {
			printk(KERN_WARNING
			       "Xenomai: %s[%d] cannot map MAYDAY page\n",
			       current->comm, current->pid);
			xnarch_free_host_mem(p, sizeof(*p));
			return ERR_PTR(-ENOMEM);
		}

		exe_path = get_exe_path(current);
		if (IS_ERR(exe_path)) {
			printk(KERN_WARNING
			       "Xenomai: %s[%d] can't find exe path\n",
			       current->comm, current->pid);
			exe_path = NULL; /* Not lethal, but weird. */
		}
		p->exe_path = exe_path;

		return &p->ppd;

	case XNSHADOW_CLIENT_DETACH:
		p = ppd2sys(data);
		xnheap_destroy_mapped(&p->sem_heap, post_ppd_release, NULL);
		if (p->exe_path)
			kfree(p->exe_path);

		return NULL;
	}

	return ERR_PTR(-EINVAL);
}

static struct xnskin_props __props = {
	.name = "sys",
	.magic = 0x434F5245,
	.nrcalls = sizeof(__systab) / sizeof(__systab[0]),
	.systab = __systab,
	.eventcb = xnshadow_sys_event,
};

static inline int
substitute_linux_syscall(struct pt_regs *regs)
{
	/* No real-time replacement for now -- let Linux handle this call. */
	return 0;
}

void xnshadow_send_sig(xnthread_t *thread, int sig, int arg, int specific)
{
	schedule_linux_call(specific ? LO_SIGTHR_REQ : LO_SIGGRP_REQ,
			    xnthread_user_task(thread),
			    xnshadow_sig_mux(sig, specific ? arg : 0));
}
EXPORT_SYMBOL_GPL(xnshadow_send_sig);

static inline
int do_hisyscall_event(unsigned event, rthal_pipeline_stage_t *stage,
		       void *data)
{
	int muxid, muxop, switched, err, sigs;
	struct pt_regs *regs = data;
	struct task_struct *p;
	struct xnsysent *se;
	xnthread_t *thread;
	u_long sysflags;

	if (!xnpod_active_p())
		goto no_skin;

	xnarch_hisyscall_entry();

	p = current;
	thread = xnshadow_thread(p);
	if (thread)
		thread->regs = regs;

	if (!__xn_reg_mux_p(regs))
		goto linux_syscall;

	/*
	 * Executing Xenomai services requires CAP_SYS_NICE, except for
	 * __xn_sys_bind which does its own checks.
	 */
	if (unlikely(!cap_raised(current_cap(), CAP_SYS_NICE)) &&
	    __xn_reg_mux(regs) != __xn_mux_code(0, __xn_sys_bind))
		goto no_permission;

	muxid = __xn_mux_id(regs);
	muxop = __xn_mux_op(regs);

	trace_mark(xn_nucleus, syscall_histage_entry,
		   "thread %p thread_name %s muxid %d muxop %d",
		   thread, thread ? xnthread_name(thread) : NULL,
		   muxid, muxop);

	if (muxid < 0 || muxid > XENOMAI_SKINS_NR ||
	    muxop < 0 || muxop >= skins[muxid].props->nrcalls) {
		__xn_error_return(regs, -ENOSYS);
		goto ret_handled;
	}

	se = &skins[muxid].props->systab[muxop];
	sysflags = se->flags;

	if ((sysflags & __xn_exec_shadow) != 0 && thread == NULL) {
	no_permission:
		if (XENO_DEBUG(NUCLEUS))
			printk(KERN_WARNING
			       "Xenomai: non-shadow %s[%d] was denied a real-time call\n",
			       current->comm, current->pid);
		__xn_error_return(regs, -EPERM);
		goto ret_handled;
	}

	if ((sysflags & __xn_exec_conforming) != 0)
		/* If the conforming exec bit has been set, turn the exec
		   bitmask for the syscall into the most appropriate setup for
		   the caller, i.e. Xenomai domain for shadow threads, Linux
		   otherwise. */
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

      restart:			/* Process adaptive syscalls by restarting them in the
				   opposite domain. */

	if ((sysflags & __xn_exec_lostage) != 0) {
		/* Syscall must run into the Linux domain. */

		if (stage == &rthal_domain) {
			/* Request originates from the Xenomai domain: just relax the
			   caller and execute the syscall immediately after. */
			xnshadow_relax(1, SIGDEBUG_MIGRATE_SYSCALL);
			switched = 1;
		} else
			/* Request originates from the Linux domain: propagate the
			   event to our Linux-based handler, so that the syscall
			   is executed from there. */
			goto propagate_syscall;
	} else if ((sysflags & (__xn_exec_histage | __xn_exec_current)) != 0) {
		/* Syscall must be processed either by Xenomai, or by the
		   calling domain. */

		if (stage != &rthal_domain)
			/* Request originates from the Linux domain: propagate the
			   event to our Linux-based handler, so that the caller is
			   hardened and the syscall is eventually executed from
			   there. */
			goto propagate_syscall;

		/* Request originates from the Xenomai domain: run the syscall
		   immediately. */
	}

	err = se->svc(__xn_reg_arglist(regs));
	if (err == -ENOSYS && (sysflags & __xn_exec_adaptive) != 0) {
		if (switched) {
			switched = 0;

			if ((err = xnshadow_harden()) != 0)
				goto done;
		}

		sysflags ^=
		    (__xn_exec_lostage | __xn_exec_histage |
		     __xn_exec_adaptive);
		goto restart;
	}

      done:

	__xn_status_return(regs, err);

	sigs = 0;
	if (xnpod_shadow_p()) {
		if (signal_pending(p) ||
		    xnthread_test_info(thread, XNKICKED)) {
			sigs = 1;
			request_syscall_restart(thread, regs, sysflags);
		} else if (xnthread_test_state(thread, XNOTHER) &&
			   xnthread_get_rescnt(thread) == 0) {
			if (switched)
				switched = 0;
			else
				xnshadow_relax(0, 0);
		}
	}
	if (!sigs && (sysflags & __xn_exec_switchback) != 0 && switched)
		xnshadow_harden();	/* -EPERM will be trapped later if needed. */

      ret_handled:

	/* Update the stats and userland-visible state. */
	if (thread) {
		xnstat_counter_inc(&thread->stat.xsc);
		*thread->u_mode = thread->state;
	}

	trace_mark(xn_nucleus, syscall_histage_exit,
		   "ret %ld", __xn_reg_rval(regs));
	return RTHAL_EVENT_STOP;

      linux_syscall:

	if (xnpod_root_p())
		/*
		 * The call originates from the Linux domain, either
		 * from a relaxed shadow or from a regular Linux task;
		 * just propagate the event so that we will fall back
		 * to linux_sysentry().
		 */
		goto propagate_syscall;

	/*
	 * From now on, we know that we have a valid shadow thread
	 * pointer.
	 */
	if (substitute_linux_syscall(regs))
		/*
		 * This is a Linux syscall issued on behalf of a
		 * shadow thread running inside the Xenomai
		 * domain. This call has just been intercepted by the
		 * nucleus and a Xenomai replacement has been
		 * substituted for it.
		 */
		goto ret_handled;

	/*
	 * This syscall has not been substituted, let Linux handle
	 * it. This will eventually fall back to the Linux syscall
	 * handler if our Linux domain handler does not intercept
	 * it. Before we let it go, ensure that the current thread has
	 * properly entered the Linux domain.
	 */
	xnshadow_relax(1, SIGDEBUG_MIGRATE_SYSCALL);

	goto propagate_syscall;

      no_skin:

	if (__xn_reg_mux_p(regs)) {
		if (__xn_reg_mux(regs) == __xn_mux_code(0, __xn_sys_bind))
			/*
			 * Valid exception case: we may be called to
			 * bind to a skin which will create its own
			 * pod through its callback routine before
			 * returning to user-space.
			 */
			goto propagate_syscall;

		xnlogwarn("bad syscall %ld/%ld -- no skin loaded.\n",
			  __xn_mux_id(regs), __xn_mux_op(regs));

		__xn_error_return(regs, -ENOSYS);
		return RTHAL_EVENT_STOP;
	}

	/*
	 * Regular Linux syscall with no skin loaded -- propagate it
	 * to the Linux kernel.
	 */

      propagate_syscall:

	return RTHAL_EVENT_PROPAGATE;
}

RTHAL_DECLARE_EVENT(hisyscall_event);

static inline
int do_losyscall_event(unsigned event, rthal_pipeline_stage_t *stage,
		       void *data)
{
	int muxid, muxop, sysflags, switched, err, sigs;
	xnthread_t *thread = xnshadow_thread(current);
	struct pt_regs *regs = data;
	struct xnsysent *se;

	if (__xn_reg_mux_p(regs))
		goto xenomai_syscall;

	if (thread == NULL || !substitute_linux_syscall(regs))
		/* Fall back to Linux syscall handling. */
		return RTHAL_EVENT_PROPAGATE;

	/*
	 * This is a Linux syscall issued on behalf of a shadow thread
	 * running inside the Linux domain. If the call has been
	 * substituted with a Xenomai replacement, do not let Linux know
	 * about it.
	 */
	return RTHAL_EVENT_STOP;

      xenomai_syscall:

	/* muxid and muxop have already been checked in the Xenomai domain
	   handler. */

	if (thread)
		thread->regs = regs;

	muxid = __xn_mux_id(regs);
	muxop = __xn_mux_op(regs);

	trace_mark(xn_nucleus, syscall_lostage_entry,
		   "thread %p thread_name %s muxid %d muxop %d",
		   xnpod_active_p() ? xnpod_current_thread() : NULL,
		   xnpod_active_p() ? xnthread_name(xnpod_current_thread()) : NULL,
		   muxid, muxop);

	/* Processing a real-time skin syscall. */

	se = &skins[muxid].props->systab[muxop];
	sysflags = se->flags;

	if ((sysflags & __xn_exec_conforming) != 0)
		sysflags |= (thread ? __xn_exec_histage : __xn_exec_lostage);

      restart:			/* Process adaptive syscalls by restarting them in the
				   opposite domain. */

	if ((sysflags & __xn_exec_histage) != 0) {
		/* This request originates from the Linux domain and must be
		   run into the Xenomai domain: harden the caller and execute the
		   syscall. */
		if ((err = xnshadow_harden()) != 0) {
			__xn_error_return(regs, err);
			goto ret_handled;
		}

		switched = 1;
	} else			/* We want to run the syscall in the Linux domain.  */
		switched = 0;

	err = se->svc(__xn_reg_arglist(regs));
	if (err == -ENOSYS && (sysflags & __xn_exec_adaptive) != 0) {
		if (switched) {
			switched = 0;
			xnshadow_relax(1, SIGDEBUG_MIGRATE_SYSCALL);
		}

		sysflags ^=
		    (__xn_exec_lostage | __xn_exec_histage |
		     __xn_exec_adaptive);
		goto restart;
	}

	__xn_status_return(regs, err);

	sigs = 0;
	if (xnpod_active_p() && xnpod_shadow_p()) {
		/*
		 * We may have gained a shadow TCB from the syscall we
		 * just invoked, so make sure to fetch it.
		 */
		thread = xnshadow_thread(current);
		if (signal_pending(current)) {
			sigs = 1;
			request_syscall_restart(thread, regs, sysflags);
		} else if (xnthread_test_state(thread, XNOTHER) &&
			   xnthread_get_rescnt(thread) == 0)
			sysflags |= __xn_exec_switchback;
	}
	if (!sigs && (sysflags & __xn_exec_switchback) != 0
	    && (switched || xnpod_primary_p()))
		xnshadow_relax(0, 0);

      ret_handled:

	/* Update the stats and userland-visible state. */
	if (thread) {
		xnstat_counter_inc(&thread->stat.xsc);
		*thread->u_mode = thread->state;
	}

	trace_mark(xn_nucleus, syscall_lostage_exit,
		   "ret %ld", __xn_reg_rval(regs));

	return RTHAL_EVENT_STOP;
}

RTHAL_DECLARE_EVENT(losyscall_event);

static inline void do_taskexit_event(struct task_struct *p)
{
	xnthread_t *thread = xnshadow_thread(p); /* p == current */
	struct xnsys_ppd *sys_ppd;
	unsigned magic;
	spl_t s;

	if (!thread)
		return;

	XENO_BUGON(NUCLEUS, !xnpod_root_p());

	if (xnthread_test_state(thread, XNDEBUG))
		unlock_timers();

	magic = xnthread_get_magic(thread);

	xnlock_get_irqsave(&nklock, s);
	/* Prevent wakeup call from xnshadow_unmap(). */
	xnshadow_thrptd(p) = NULL;
	xnthread_archtcb(thread)->user_task = NULL;
	/* xnpod_delete_thread() -> hook -> xnshadow_unmap(). */
	xnsched_set_resched(thread->sched);
	xnpod_delete_thread(thread);
	sys_ppd = xnsys_ppd_get(0);
	xnlock_put_irqrestore(&nklock, s);
	xnpod_schedule();

	if (!xnarch_atomic_get(&sys_ppd->refcnt))
		ppd_remove_mm(xnshadow_mm(p), &detach_ppd);

	trace_mark(xn_nucleus, shadow_exit, "thread %p thread_name %s",
		   thread, xnthread_name(thread));
}

RTHAL_DECLARE_EXIT_EVENT(taskexit_event);

static inline void do_schedule_event(struct task_struct *next_task)
{
	struct task_struct *prev_task;
	struct xnthread *prev, *next;
	sigset_t pending;

	if (!xnpod_active_p())
		return;

	prev_task = current;
	prev = xnshadow_thread(prev_task);
	next = xnshadow_thread(next_task);
	set_switch_lock_owner(prev_task);

	if (next) {
		/*
		 * Check whether we need to unlock the timers, each
		 * time a Linux task resumes from a stopped state,
		 * excluding tasks resuming shortly for entering a
		 * stopped state asap due to ptracing. To identify the
		 * latter, we need to check for SIGSTOP and SIGINT in
		 * order to encompass both the NPTL and LinuxThreads
		 * behaviours.
		 */
		if (xnthread_test_state(next, XNDEBUG)) {
			if (signal_pending(next_task)) {
				/*
				 * Do not grab the sighand lock here:
				 * it's useless, and we already own
				 * the runqueue lock, so this would
				 * expose us to deadlock situations on
				 * SMP.
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
			int sigpending = signal_pending(next_task);

			if (!xnthread_test_state(next, XNRELAX)) {
				xnarch_trace_panic_freeze();
				show_stack(xnthread_user_task(next), NULL);
				xnpod_fatal
				    ("Hardened thread %s[%d] running in Linux"" domain?! (status=0x%lx, sig=%d, prev=%s[%d])",
				     next->name, next_task->pid, xnthread_state_flags(next),
				     sigpending, prev_task->comm, prev_task->pid);
			} else if (!(next_task->ptrace & PT_PTRACED) &&
				   /* Allow ptraced threads to run shortly in order to
				      properly recover from a stopped state. */
				   xnthread_test_state(next, XNSTARTED)
				   && xnthread_test_state(next, XNPEND)) {
				xnarch_trace_panic_freeze();
				show_stack(xnthread_user_task(next), NULL);
				xnpod_fatal
				    ("blocked thread %s[%d] rescheduled?! (status=0x%lx, sig=%d, prev=%s[%d])",
				     next->name, next_task->pid, xnthread_state_flags(next),
				     sigpending, prev_task->comm, prev_task->pid);
			}
		}
	}
}

RTHAL_DECLARE_SCHEDULE_EVENT(schedule_event);

static inline void do_sigwake_event(struct task_struct *p)
{
	struct xnthread *thread = xnshadow_thread(p);
	sigset_t pending;
	spl_t s;

	if (thread == NULL)
		return;

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
		return;
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
		set_task_nowakeup(p);

	xnshadow_force_wakeup(thread);

	xnpod_schedule();

	xnlock_put_irqrestore(&nklock, s);
}

RTHAL_DECLARE_SIGWAKE_EVENT(sigwake_event);

static inline void do_setsched_event(struct task_struct *p, int priority)
{
	xnthread_t *thread = xnshadow_thread(p);
	union xnsched_policy_param param;
	struct xnsched *sched;

	if (!thread || (p->policy != SCHED_FIFO && p->policy != SCHED_NORMAL))
		return;

	if (p->policy == SCHED_NORMAL)
		priority = 0;

	/*
	 * Linux's priority scale is a subset of the core pod's
	 * priority scale, so there is no need to bound the priority
	 * values when mapping them from Linux -> Xenomai. We
	 * propagate priority changes to the nucleus only for threads
	 * that belong to skins that have a compatible priority scale.
	 *
	 * BIG FAT WARNING: Change of scheduling parameters from the
	 * Linux side are propagated only to threads that belong to
	 * the Xenomai RT scheduling class. Threads from other classes
	 * are remain unaffected, since we could not map this
	 * information 1:1 between Linux and Xenomai.
	 */
	if (thread->base_class != &xnsched_class_rt ||
	    thread->cprio == priority)
		return;

	if (xnthread_get_denormalized_prio(thread, priority) != priority)
		/* Priority scales don't match 1:1. */
		return;

	param.rt.prio = priority;
	__xnpod_set_thread_schedparam(thread, &xnsched_class_rt, &param, 0);
	sched = xnpod_current_sched();

	if (xnsched_resched_p(sched))
		xnpod_schedule();
}

RTHAL_DECLARE_SETSCHED_EVENT(setsched_event);

static inline void do_cleanup_event(struct mm_struct *mm)
{
	struct task_struct *p = current;
	struct xnsys_ppd *sys_ppd;
	struct mm_struct *old;

	old = xnshadow_mm(p);
	xnshadow_mmptd(p) = mm;

	sys_ppd = xnsys_ppd_get(0);
	if (sys_ppd == &__xnsys_global_ppd)
		goto done;

	if (xnarch_atomic_dec_and_test(&sys_ppd->refcnt))
		ppd_remove_mm(mm, &detach_ppd);

  done:
	xnshadow_mmptd(p) = old;
}

RTHAL_DECLARE_CLEANUP_EVENT(cleanup_event);

/*
 * xnshadow_register_interface() -- Register a new skin/interface.
 * NOTE: an interface can be registered without its pod being
 * necessarily active. In such a case, a lazy initialization scheme
 * can be implemented through the event callback fired upon the first
 * client binding.
 *
 * The event callback will be called with its first argument set to:
 * - XNSHADOW_CLIENT_ATTACH, when a user-space process binds the interface, the
 *   second argument being the task_struct pointer of the calling thread, the
 *   callback may then return:
 *   . a pointer to an xnshadow_ppd_t structure, meaning that this structure
 *   will be attached to the calling process for this interface;
 *   . a NULL pointer, meaning that no per-process structure should be attached
 *   to this process for this interface;
 *   . ERR_PTR(negative value) indicating an error, the binding process will
 *   then abort;
 * - XNSHADOW_DETACH, when a user-space process terminates, if a non-NULL
 *   per-process structure is attached to the dying process, the second argument
 *   being the pointer to the per-process data attached to the dying process.
 */

int xnshadow_register_interface(struct xnskin_props *props)
{
	struct xnskin_slot *sslt;
	int muxid;
	spl_t s;

	/*
	 * We can only handle up to MAX_SYSENT syscalls per skin,
	 * check for over- and underflow (MKL).
	 */
	if (XENOMAI_MAX_SYSENT < props->nrcalls || 0 > props->nrcalls)
		return -EINVAL;

	down(&registration_mutex);

	xnlock_get_irqsave(&nklock, s);

	for (muxid = 0; muxid < XENOMAI_SKINS_NR; muxid++) {
		sslt = skins + muxid;
		if (sslt->props == NULL) {
			sslt->props = props;
			break;
		}
	}

	xnlock_put_irqrestore(&nklock, s);

	if (muxid >= XENOMAI_SKINS_NR) {
		up(&registration_mutex);
		return -EAGAIN;
	}

	up(&registration_mutex);

	return muxid;

}
EXPORT_SYMBOL_GPL(xnshadow_register_interface);

/*
 * xnshadow_unregister_interface() -- Unregister a skin/interface.
 * NOTE: an interface can be unregistered without its pod being
 * necessarily active.
 */
int xnshadow_unregister_interface(int muxid)
{
	struct xnskin_slot *sslt;
	spl_t s;

	if (muxid < 0 || muxid >= XENOMAI_SKINS_NR)
		return -EINVAL;

	sslt = skins + muxid;

	down(&registration_mutex);

	xnlock_get_irqsave(&nklock, s);

	sslt->props = NULL;

	xnlock_put_irqrestore(&nklock, s);

	up(&registration_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(xnshadow_unregister_interface);

/**
 * Return the per-process data attached to the calling process.
 *
 * This service returns the per-process data attached to the calling process for
 * the skin whose muxid is @a muxid. It must be called with nklock locked, irqs
 * off.
 *
 * See xnshadow_register_interface() documentation for information on the way to
 * attach a per-process data to a process.
 *
 * @param muxid the skin muxid.
 *
 * @return the per-process data if the current context is a user-space process;
 * @return NULL otherwise.
 *
 */
xnshadow_ppd_t *xnshadow_ppd_get(unsigned int muxid)
{
	if (xnpod_userspace_p())
		return ppd_lookup(muxid, xnshadow_mm(current) ?: current->mm);

	return NULL;
}
EXPORT_SYMBOL_GPL(xnshadow_ppd_get);

void xnshadow_grab_events(void)
{
	rthal_catch_taskexit(&taskexit_event);
	rthal_catch_sigwake(&sigwake_event);
	rthal_catch_schedule(&schedule_event);
	rthal_catch_setsched(&setsched_event);
	rthal_catch_cleanup(&cleanup_event);
	rthal_catch_return(&mayday_event);
}

void xnshadow_release_events(void)
{
	rthal_catch_taskexit(NULL);
	rthal_catch_sigwake(NULL);
	rthal_catch_schedule(NULL);
	rthal_catch_setsched(NULL);
	rthal_catch_cleanup(NULL);
	rthal_catch_return(NULL);
}

int xnshadow_mount(void)
{
	struct xnsched *sched;
	unsigned i, size;
	int cpu, ret;

	sema_init(&completion_mutex, 1);
	nkthrptd = rthal_alloc_ptdkey();
	nkerrptd = rthal_alloc_ptdkey();
	nkmmptd = rthal_alloc_ptdkey();

	if (nkthrptd < 0 || nkerrptd < 0 || nkmmptd < 0) {
		printk(KERN_ERR "Xenomai: cannot allocate PTD slots\n");
		return -ENOMEM;
	}

	ret = xndebug_init();
	if (ret)
		return ret;

	lostage_apc =
	    rthal_apc_alloc("lostage_handler", &lostage_handler, NULL);

	for_each_online_cpu(cpu) {
		if (!xnarch_cpu_supported(cpu))
			continue;

		sched = &nkpod_struct.sched[cpu];
		sema_init(&sched->gksync, 0);
		xnarch_memory_barrier();
		sched->gatekeeper =
		    kthread_create(&gatekeeper_thread, (void *)(long)cpu,
				   "gatekeeper/%d", cpu);
		wake_up_process(sched->gatekeeper);
		down(&sched->gksync);
	}

	/*
	 * Setup the mayday page early, before userland can mess with
	 * real-time ops.
	 */
	ret = mayday_init_page();
	if (ret) {
		xnshadow_cleanup();
		return ret;
	}

	/* We need to grab these ones right now. */
	rthal_catch_losyscall(&losyscall_event);
	rthal_catch_hisyscall(&hisyscall_event);

	size = sizeof(xnqueue_t) * PPD_HASH_SIZE;
	ppd_hash = (xnqueue_t *)xnarch_alloc_host_mem(size);
	if (!ppd_hash) {
		xnshadow_cleanup();
		printk(KERN_WARNING
		       "Xenomai: cannot allocate PPD hash table.\n");
		return -ENOMEM;
	}

	for (i = 0; i < PPD_HASH_SIZE; i++)
		initq(&ppd_hash[i]);

	nucleus_muxid = xnshadow_register_interface(&__props);

	if (nucleus_muxid != 0) {
		if (nucleus_muxid > 0) {
			printk(KERN_WARNING
			       "Xenomai: got non null id when registering "
			       "nucleus syscall table.\n");
		} else
			printk(KERN_WARNING
			       "Xenomai: cannot register nucleus syscall table.\n");

		xnshadow_cleanup();
		return -ENOMEM;
	}

	return 0;
}

void xnshadow_cleanup(void)
{
	struct xnsched *sched;
	int cpu;

	if (nucleus_muxid >= 0) {
		xnshadow_unregister_interface(nucleus_muxid);
		nucleus_muxid = -1;
	}

	if (ppd_hash)
		xnarch_free_host_mem(ppd_hash,
			       sizeof(xnqueue_t) * PPD_HASH_SIZE);
	ppd_hash = NULL;

	rthal_catch_losyscall(NULL);
	rthal_catch_hisyscall(NULL);

	for_each_online_cpu(cpu) {
		if (!xnarch_cpu_supported(cpu))
			continue;

		sched = &nkpod_struct.sched[cpu];
		down(&sched->gksync);
		sched->gktarget = NULL;
		kthread_stop(sched->gatekeeper);
	}

	rthal_apc_free(lostage_apc);
	rthal_free_ptdkey(nkerrptd);
	rthal_free_ptdkey(nkthrptd);

	mayday_cleanup_page();

	xndebug_cleanup();
}

/*@}*/

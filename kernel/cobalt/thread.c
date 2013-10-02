/*
 * Copyright (C) 2001-2013 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) 2006-2010 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>
 * Copyright (C) 2001-2013 The Xenomai project <http://www.xenomai.org>
 *
 * SMP support Copyright (C) 2004 The HYADES project <http://www.hyades-itea.org>
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
#include <linux/kthread.h>
#include <linux/wait.h>
#include <cobalt/kernel/sched.h>
#include <cobalt/kernel/timer.h>
#include <cobalt/kernel/synch.h>
#include <cobalt/kernel/heap.h>
#include <cobalt/kernel/intr.h>
#include <cobalt/kernel/registry.h>
#include <cobalt/kernel/clock.h>
#include <cobalt/kernel/stat.h>
#include <cobalt/kernel/trace.h>
#include <cobalt/kernel/assert.h>
#include <cobalt/kernel/select.h>
#include <cobalt/kernel/shadow.h>
#include <cobalt/kernel/lock.h>
#include <cobalt/kernel/thread.h>
#include <asm/xenomai/thread.h>

/**
 * @ingroup nucleus
 * @defgroup thread Thread services.
 * @{
*/

static unsigned int idtags;

static DECLARE_WAIT_QUEUE_HEAD(nkjoinq);

static void timeout_handler(struct xntimer *timer)
{
	struct xnthread *thread = container_of(timer, xnthread_t, rtimer);

	xnthread_set_info(thread, XNTIMEO);	/* Interrupts are off. */
	xnthread_resume(thread, XNDELAY);
}

static void periodic_handler(struct xntimer *timer)
{
	struct xnthread *thread = container_of(timer, xnthread_t, ptimer);
	/*
	 * Prevent unwanted round-robin, and do not wake up threads
	 * blocked on a resource.
	 */
	if (xnthread_test_state(thread, XNDELAY|XNPEND) == XNDELAY)
		xnthread_resume(thread, XNDELAY);
	/*
	 * The thread a periodic timer is affine to might have been
	 * migrated to another CPU while passive. Fix this up.
	 */
	xntimer_set_sched(timer, thread->sched);
}

struct kthread_arg {
	struct xnthread *thread;
	struct completion *done;
};

static int kthread_trampoline(void *arg)
{
	struct kthread_arg *ka = arg;
	struct xnthread *thread = ka->thread;
	struct sched_param param;
	int ret, policy, prio;

	/*
	 * It only makes sense to create Xenomai kthreads with the
	 * SCHED_FIFO, SCHED_NORMAL or SCHED_WEAK policies. So
	 * anything that is not from Xenomai's RT class is assumed to
	 * belong to SCHED_NORMAL linux-wise.
	 */
	if (thread->sched_class != &xnsched_class_rt) {
		policy = SCHED_NORMAL;
		prio = 0;
	} else {
		policy = SCHED_FIFO;
		prio = normalize_priority(thread->cprio);
	}

	param.sched_priority = prio;
	sched_setscheduler(current, policy, &param);

	ret = xnshadow_map_kernel(thread, ka->done);
	if (ret) {
		printk(XENO_WARN "failed to create kernel shadow %s\n",
		       thread->name);
		return ret;
	}

	trace_mark(xn_nucleus, thread_boot, "thread %p thread_name %s",
		   thread, xnthread_name(thread));

	thread->entry(thread->cookie);

	xnthread_cancel(thread);

	return 0;
}

static inline int spawn_kthread(struct xnthread *thread)
{
	DECLARE_COMPLETION_ONSTACK(done);
	struct kthread_arg ka = {
		.thread = thread,
		.done = &done
	};
	struct task_struct *p;

	p = kthread_run(kthread_trampoline, &ka, "%s", thread->name);
	if (IS_ERR(p))
		return PTR_ERR(p);

	wait_for_completion(&done);

	return 0;
}

int __xnthread_init(struct xnthread *thread,
		    const struct xnthread_init_attr *attr,
		    struct xnsched *sched,
		    struct xnsched_class *sched_class,
		    const union xnsched_policy_param *sched_param)
{
	DECLARE_COMPLETION_ONSTACK(done);
	int flags = attr->flags, ret;
	spl_t s;

	flags &= ~XNSUSP;
#ifndef CONFIG_XENO_HW_FPU
	flags &= ~XNFPU;
#endif
	if (flags & XNROOT)
		thread->idtag = 0;
	else {
		xnlock_get_irqsave(&nklock, s);
		thread->idtag = ++idtags ?: 1;
		xnlock_put_irqrestore(&nklock, s);
		flags |= XNDORMANT;
	}

	if (attr->name)
		xnobject_copy_name(thread->name, attr->name);
	else
		snprintf(thread->name, sizeof(thread->name), "%p", thread);

	thread->personality = attr->personality;
	cpus_and(thread->affinity, attr->affinity, nkaffinity);
	thread->sched = sched;
	thread->state = flags;
	thread->info = 0;
	thread->schedlck = 0;
	thread->rrperiod = XN_INFINITE;
	thread->wchan = NULL;
	thread->wwake = NULL;
	thread->wcontext = NULL;
	thread->hrescnt = 0;
	thread->registry.handle = XN_NO_HANDLE;
	thread->registry.waitkey = NULL;
	memset(&thread->stat, 0, sizeof(thread->stat));
	thread->selector = NULL;
	INIT_LIST_HEAD(&thread->claimq);
	xnsynch_init(&thread->join_synch, XNSYNCH_FIFO, NULL);
	/* These will be filled by xnthread_start() */
	thread->imode = 0;
	thread->entry = NULL;
	thread->cookie = NULL;

	xntimer_init(&thread->rtimer, &nkclock, timeout_handler, thread);
	xntimer_set_name(&thread->rtimer, thread->name);
	xntimer_set_priority(&thread->rtimer, XNTIMER_HIPRIO);
	xntimer_init(&thread->ptimer, &nkclock, periodic_handler, thread);
	xntimer_set_name(&thread->ptimer, thread->name);
	xntimer_set_priority(&thread->ptimer, XNTIMER_HIPRIO);

	thread->init_class = sched_class;
	thread->base_class = NULL; /* xnsched_set_policy() will set it. */
	thread->init_schedparam = *sched_param;
	ret = xnsched_init_thread(thread);
	if (ret)
		return ret;

	ret = xnsched_set_policy(thread, sched_class, sched_param);
	if (ret)
		return ret;

	if ((flags & (XNUSER|XNROOT)) == 0)
		ret = spawn_kthread(thread);

	return ret;
}

void xnthread_init_shadow_tcb(struct xnthread *thread, struct task_struct *task)
{
	struct xnarchtcb *tcb = xnthread_archtcb(thread);

	memset(tcb, 0, sizeof(*tcb));
	tcb->core.host_task = task;
	tcb->core.tsp = &task->thread;
	tcb->core.mm = task->mm;
	tcb->core.active_mm = task->mm;
#ifdef CONFIG_XENO_HW_UNLOCKED_SWITCH
	tcb->core.tip = task_thread_info(task);
#endif
#ifdef CONFIG_XENO_HW_FPU
	tcb->core.user_fpu_owner = task;
#endif /* CONFIG_XENO_HW_FPU */
	xnarch_init_shadow_tcb(tcb);
}

void xnthread_init_root_tcb(struct xnthread *thread)
{
	struct xnarchtcb *tcb = xnthread_archtcb(thread);

	memset(tcb, 0, sizeof(*tcb));
	tcb->core.host_task = current;
	tcb->core.tsp = &tcb->core.ts;
	tcb->core.mm = current->mm;
#ifdef CONFIG_XENO_HW_UNLOCKED_SWITCH
	tcb->core.tip = &tcb->core.ti;
#endif
	xnarch_init_root_tcb(tcb);
}

void xnthread_deregister(struct xnthread *thread)
{
	if (thread->registry.handle != XN_NO_HANDLE)
		xnregistry_remove(thread->registry.handle);

	thread->registry.handle = XN_NO_HANDLE;
}

char *xnthread_format_status(unsigned long status, char *buf, int size)
{
	static const char labels[] = XNTHREAD_STATE_LABELS;
	int pos, c, mask;
	char *wp;

	for (mask = (int)status, pos = 0, wp = buf;
	     mask != 0 && wp - buf < size - 2;	/* 1-letter label + \0 */
	     mask >>= 1, pos++) {
		if ((mask & 1) == 0)
			continue;

		c = labels[pos];

		switch (1 << pos) {
		case XNROOT:
			c = 'R'; /* Always mark root as runnable. */
			break;
		case XNREADY:
			if (status & XNROOT)
				continue; /* Already reported on XNROOT. */
			break;
		case XNDELAY:
			/*
			 * Only report genuine delays here, not timed
			 * waits for resources.
			 */
			if (status & XNPEND)
				continue;
			break;
		case XNPEND:
			/* Report timed waits with lowercase symbol. */
			if (status & XNDELAY)
				c |= 0x20;
			break;
		default:
			if (c == '.')
				continue;
		}
		*wp++ = c;
	}

	*wp = '\0';

	return buf;
}

xnticks_t xnthread_get_timeout(xnthread_t *thread, xnticks_t ns)
{
	struct xntimer *timer;
	xnticks_t timeout;

	if (!xnthread_test_state(thread,XNDELAY))
		return 0LL;

	if (xntimer_running_p(&thread->rtimer))
		timer = &thread->rtimer;
	else if (xntimer_running_p(&thread->ptimer))
		timer = &thread->ptimer;
	else
		return 0LL;

	timeout = xntimer_get_date(timer);
	if (timeout <= ns)
		return 1;

	return timeout - ns;
}
EXPORT_SYMBOL_GPL(xnthread_get_timeout);

xnticks_t xnthread_get_period(xnthread_t *thread)
{
	xnticks_t period = 0;
	/*
	 * The current thread period might be:
	 * - the value of the timer interval for periodic threads (ns/ticks)
	 * - or, the value of the alloted round-robin quantum (ticks)
	 * - or zero, meaning "no periodic activity".
	 */
	if (xntimer_running_p(&thread->ptimer))
		period = xntimer_get_interval(&thread->ptimer);
	else if (xnthread_test_state(thread,XNRRB))
		period = xnthread_time_slice(thread);

	return period;
}
EXPORT_SYMBOL_GPL(xnthread_get_period);

void xnthread_prepare_wait(struct xnthread_wait_context *wc)
{
	struct xnthread *curr = xnsched_current_thread();

	wc->posted = 0;
	curr->wcontext = wc;
}
EXPORT_SYMBOL_GPL(xnthread_prepare_wait);

static inline int moving_target(struct xnsched *sched, struct xnthread *thread)
{
	int ret = 0;
#ifdef CONFIG_XENO_HW_UNLOCKED_SWITCH
	/*
	 * When deleting a thread in the course of a context switch or
	 * in flight to another CPU with nklock unlocked on a distant
	 * CPU, do nothing, this case will be caught in
	 * xnsched_finish_unlocked_switch.
	 */
	ret = (sched->status & XNINSW) ||
		xnthread_test_state(thread, XNMIGRATE);
#endif
	return ret;
}

#ifdef CONFIG_XENO_HW_FPU

static inline void giveup_fpu(struct xnsched *sched,
			      struct xnthread *thread)
{
	if (thread == sched->fpuholder)
		sched->fpuholder = NULL;
}

static inline void release_fpu(struct xnthread *thread)
{
	/*
	 * Force the FPU save, and nullify the sched->fpuholder
	 * pointer, to avoid leaving fpuholder pointing on the backup
	 * area of the migrated thread.
	 */
	if (xnthread_test_state(thread, XNFPU)) {
		xnarch_save_fpu(xnthread_archtcb(thread));
		thread->sched->fpuholder = NULL;
	}
}

void xnthread_switch_fpu(struct xnsched *sched)
{
	xnthread_t *curr = sched->curr;

	if (!xnthread_test_state(curr, XNFPU))
		return;

	if (sched->fpuholder != curr) {
		if (sched->fpuholder == NULL ||
		    xnarch_fpu_ptr(xnthread_archtcb(sched->fpuholder)) !=
		    xnarch_fpu_ptr(xnthread_archtcb(curr))) {
			if (sched->fpuholder)
				xnarch_save_fpu(xnthread_archtcb
						(sched->fpuholder));

			xnarch_restore_fpu(xnthread_archtcb(curr));
		} else
			xnarch_enable_fpu(xnthread_archtcb(curr));

		sched->fpuholder = curr;
	} else
		xnarch_enable_fpu(xnthread_archtcb(curr));
}

#else /* !CONFIG_XENO_HW_FPU */

static inline void giveup_fpu(struct xnsched *sched,
				      struct xnthread *thread)
{
}

static inline void release_fpu(struct xnthread *thread)
{
}

#endif /* !CONFIG_XENO_HW_FPU */

static inline void cleanup_tcb(struct xnthread *thread) /* nklock held, irqs off */
{
	struct xnsched *sched = thread->sched;

	list_del(&thread->glink);
	nknrthreads--;
	xnvfile_touch_tag(&nkthreadlist_tag);

	if (xnthread_test_state(thread, XNREADY)) {
		XENO_BUGON(NUCLEUS, xnthread_test_state(thread, XNTHREAD_BLOCK_BITS));
		xnsched_dequeue(thread);
		xnthread_clear_state(thread, XNREADY);
	}

	thread->idtag = 0;

	if (xnthread_test_state(thread, XNPEND))
		xnsynch_forget_sleeper(thread);

	xnthread_set_state(thread, XNZOMBIE);
	/*
	 * NOTE: we must be running over the root thread, or @thread
	 * is dormant, which means that we don't risk sched->curr to
	 * disappear due to voluntary rescheduling while holding the
	 * nklock, despite @thread bears the zombie bit.
	 */
	xnsynch_release_all_ownerships(thread);

	giveup_fpu(sched, thread);

	if (moving_target(sched, thread))
		return;

	xnsched_forget(thread);
	xnthread_deregister(thread);
}

void __xnthread_cleanup(struct xnthread *curr)
{
	spl_t s;

	secondary_mode_only();

	trace_mark(xn_nucleus, thread_cleanup, "thread %p thread_name %s",
		   curr, xnthread_name(curr));

	xntimer_destroy(&curr->rtimer);
	xntimer_destroy(&curr->ptimer);

	if (curr->selector) {
		xnselector_destroy(curr->selector);
		curr->selector = NULL;
	}

	xnlock_get_irqsave(&nklock, s);
	cleanup_tcb(curr);
	xnlock_put_irqrestore(&nklock, s);

	/* Finalize last since this incurs releasing the TCB. */
	xnshadow_finalize(curr);

	wake_up(&nkjoinq);
}

/**
 * @fn void xnthread_init(struct xnthread *thread,const struct xnthread_init_attr *attr,struct xnsched_class *sched_class,const union xnsched_policy_param *sched_param)
 * @brief Initialize a new thread.
 *
 * Initializes a new thread. The thread is left dormant until it is
 * actually started by xnthread_start().
 *
 * @param thread The address of a thread descriptor the nucleus will
 * use to store the thread-specific data.  This descriptor must always
 * be valid while the thread is active therefore it must be allocated
 * in permanent memory. @warning Some architectures may require the
 * descriptor to be properly aligned in memory; this is an additional
 * reason for descriptors not to be laid in the program stack where
 * alignement constraints might not always be satisfied.
 *
 * @param attr A pointer to an attribute block describing the initial
 * properties of the new thread. Members of this structure are defined
 * as follows:
 *
 * - name: An ASCII string standing for the symbolic name of the
 * thread. This name is copied to a safe place into the thread
 * descriptor. This name might be used in various situations by the
 * nucleus for issuing human-readable diagnostic messages, so it is
 * usually a good idea to provide a sensible value here.  NULL is fine
 * though and means "anonymous".
 *
 * - flags: A set of creation flags affecting the operation. The
 * following flags can be part of this bitmask, each of them affecting
 * the nucleus behaviour regarding the created thread:
 *
 *   - XNSUSP creates the thread in a suspended state. In such a case,
 * the thread shall be explicitly resumed using the xnthread_resume()
 * service for its execution to actually begin, additionally to
 * issuing xnthread_start() for it. This flag can also be specified
 * when invoking xnthread_start() as a starting mode.
 *
 * - XNUSER shall be set if @a thread will be mapped over an existing
 * user-space task. Otherwise, a new kernel host task is created, then
 * paired with the new Xenomai thread.
 *
 * - XNFPU (enable FPU) tells the nucleus that the new thread may use
 * the floating-point unit. XNFPU is implicitly assumed for user-space
 * threads even if not set in @a flags.
 *
 * - affinity: The processor affinity of this thread. Passing
 * CPU_MASK_ALL means "any cpu" from the allowed core affinity mask
 * (nkaffinity). Passing an empty set is invalid.
 *
 * @param sched_class The initial scheduling class the new thread
 * should be assigned to.
 *
 * @param sched_param The initial scheduling parameters to set for the
 * new thread; @a sched_param must be valid within the context of @a
 * sched_class.
 *
 * @return 0 is returned on success. Otherwise, the following error
 * code indicates the cause of the failure:
 *
 * - -EINVAL is returned if @a attr->flags has invalid bits set, or @a
 *   attr->affinity is invalid (e.g. empty).
 *
 * Side-effect: This routine does not call the rescheduling procedure.
 *
 * Calling context: This service can be called from secondary mode only.
 *
 * Rescheduling: never.
 */
int xnthread_init(struct xnthread *thread,
		  const struct xnthread_init_attr *attr,
		  struct xnsched_class *sched_class,
		  const union xnsched_policy_param *sched_param)
{
	struct xnsched *sched;
	cpumask_t affinity;
	spl_t s;
	int ret;

	if (attr->flags & ~(XNFPU | XNUSER | XNSUSP))
		return -EINVAL;

	/*
	 * Pick an initial CPU for the new thread which is part of its
	 * affinity mask, and therefore also part of the supported
	 * CPUs. This CPU may change in pin_to_initial_cpu().
	 */
	cpus_and(affinity, attr->affinity, nkaffinity);
	if (cpus_empty(affinity))
		return -EINVAL;

	sched = xnsched_struct(first_cpu(affinity));

	ret = __xnthread_init(thread, attr, sched, sched_class, sched_param);
	if (ret)
		return ret;

	trace_mark(xn_nucleus, thread_init,
		   "thread %p thread_name %s flags %lu class %s prio %d",
		   thread, xnthread_name(thread), attr->flags,
		   sched_class->name, thread->cprio);

	xnlock_get_irqsave(&nklock, s);
	list_add_tail(&thread->glink, &nkthreadq);
	nknrthreads++;
	xnvfile_touch_tag(&nkthreadlist_tag);
	xnlock_put_irqrestore(&nklock, s);

	return 0;
}
EXPORT_SYMBOL_GPL(xnthread_init);

/**
 * @fn int xnthread_start(struct xnthread *thread,const struct xnthread_start_attr *attr)
 * @brief Start a newly created thread.
 *
 * Starts a (newly) created thread, scheduling it for the first
 * time. This call releases the target thread from the XNDORMANT
 * state. This service also sets the initial mode for the new thread.
 *
 * @param thread The descriptor address of the started thread which
 * must have been previously initialized by a call to xnthread_init().
 *
 * @param attr A pointer to an attribute block describing the
 * execution properties of the new thread. Members of this structure
 * are defined as follows:
 *
 * - mode: The initial thread mode. The following flags can
 * be part of this bitmask, each of them affecting the nucleus
 * behaviour regarding the started thread:
 *
 *   - XNLOCK causes the thread to lock the scheduler when it starts.
 * The target thread will have to call the xnsched_unlock()
 * service to unlock the scheduler. A non-preemptible thread may still
 * block, in which case, the lock is reasserted when the thread is
 * scheduled back in.
 *
 *   - XNSUSP makes the thread start in a suspended state. In such a
 * case, the thread will have to be explicitly resumed using the
 * xnthread_resume() service for its execution to actually begin.
 *
 * - entry: The address of the thread's body routine. In other words,
 * it is the thread entry point.
 *
 * - cookie: A user-defined opaque cookie the nucleus will pass to the
 * emerging thread as the sole argument of its entry point.
 *
 * @retval 0 if @a thread could be started ;
 *
 * @retval -EBUSY if @a thread was not dormant or stopped ;
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: possible.
 */
int xnthread_start(struct xnthread *thread,
		   const struct xnthread_start_attr *attr)
{
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (!xnthread_test_state(thread, XNDORMANT)) {
		xnlock_put_irqrestore(&nklock, s);
		return -EBUSY;
	}

	xnthread_set_state(thread, attr->mode & (XNTHREAD_MODE_BITS | XNSUSP));
	thread->imode = (attr->mode & XNTHREAD_MODE_BITS);
	thread->entry = attr->entry;
	thread->cookie = attr->cookie;

	trace_mark(xn_nucleus, thread_start, "thread %p thread_name %s",
		   thread, xnthread_name(thread));

	xnthread_resume(thread, XNDORMANT);
	xnsched_run();

	xnlock_put_irqrestore(&nklock, s);

	return 0;
}
EXPORT_SYMBOL_GPL(xnthread_start);

/**
 * @fn void xnthread_set_mode(xnthread_t *thread,int clrmask,int setmask)
 * @brief Change a thread's control mode.
 *
 * Change the control mode of a given thread. The control mode affects
 * the behaviour of the nucleus regarding the specified thread.
 *
 * @param thread The descriptor address of the affected thread.
 *
 * @param clrmask Clears the corresponding bits from the control field
 * before setmask is applied. The scheduler lock held by the current
 * thread can be forcibly released by passing the XNLOCK bit in this
 * mask. In this case, the lock nesting count is also reset to zero.
 *
 * @param setmask The new thread mode. The following flags may be set
 * in this bitmask:
 *
 * - XNLOCK makes @a thread non-preemptible by other threads when
 * running on a CPU.  A non-preemptible thread may still block, in
 * which case, the lock is reasserted when the thread is scheduled
 * back in. If @a thread is current, the scheduler is immediately
 * locked, otherwise such lock will take effect next time @a thread
 * resumes on a CPU.
 *
 * - XNTRAPSW causes the thread to receive a SIGDEBUG signal when it
 * switches to secondary mode. This is a debugging aid for detecting
 * spurious relaxes.
 *
 * - XNTRAPLB disallows breaking the scheduler lock. In the default
 * case, a thread which holds the scheduler lock is allowed to drop it
 * temporarily for sleeping. If this mode bit is set, such thread
 * would return immediately with XNBREAK set from xnthread_suspend().
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel-based task
 * - User-space task in primary mode.
 *
 * Rescheduling: possible as a result of unlocking the scheduler
 * (XNLOCK present in @a clrmask).
 *
 * @note Setting @a clrmask and @a setmask to zero leads to a nop,
 * only returning the previous mode if @a mode_r is a valid address.
 */
int xnthread_set_mode(xnthread_t *thread, int clrmask, int setmask)
{
	struct xnthread *curr = xnsched_current_thread();
	unsigned long oldmode;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	trace_mark(xn_nucleus, thread_setmode,
		   "thread %p thread_name %s clrmask 0x%x setmask 0x%x",
		   thread, xnthread_name(thread), clrmask, setmask);

	oldmode = xnthread_state_flags(thread) & XNTHREAD_MODE_BITS;
	xnthread_clear_state(thread, clrmask & XNTHREAD_MODE_BITS);
	xnthread_set_state(thread, setmask & XNTHREAD_MODE_BITS);

	/*
	 * Marking the thread as (non-)preemptible requires special
	 * handling, depending on whether @thread is current.
	 */
	if (xnthread_test_state(thread, XNLOCK)) {
		if ((oldmode & XNLOCK) == 0) {
			if (thread == curr)
				__xnsched_lock();
			else
				xnthread_lock_count(curr) = 1;
		}
	} else if (oldmode & XNLOCK) {
		if (thread == curr)
			__xnsched_unlock_fully(); /* Will resched. */
		else
			xnthread_lock_count(curr) = 0;
	}

	xnlock_put_irqrestore(&nklock, s);

	return (int)oldmode;
}
EXPORT_SYMBOL_GPL(xnthread_set_mode);

/**
 * @fn void xnthread_suspend(xnthread_t *thread, int mask,xnticks_t timeout, xntmode_t timeout_mode,xnsynch_t *wchan)
 * @brief Suspend a thread.
 *
 * Suspends the execution of a thread according to a given suspensive
 * condition. This thread will not be eligible for scheduling until it
 * all the pending suspensive conditions set by this service are
 * removed by one or more calls to xnthread_resume().
 *
 * @param thread The descriptor address of the suspended thread.
 *
 * @param mask The suspension mask specifying the suspensive condition
 * to add to the thread's wait mask. Possible values usable by the
 * caller are:
 *
 * - XNSUSP. This flag forcibly suspends a thread, regardless of any
 * resource to wait for. A reverse call to xnthread_resume()
 * specifying the XNSUSP bit must be issued to remove this condition,
 * which is cumulative with other suspension bits.@a wchan should be
 * NULL when using this suspending mode.
 *
 * - XNDELAY. This flags denotes a counted delay wait (in ticks) which
 * duration is defined by the value of the timeout parameter.
 *
 * - XNPEND. This flag denotes a wait for a synchronization object to
 * be signaled. The wchan argument must points to this object. A
 * timeout value can be passed to bound the wait. This suspending mode
 * should not be used directly by the client interface, but rather
 * through the xnsynch_sleep_on() call.
 *
 * @param timeout The timeout which may be used to limit the time the
 * thread pends on a resource. This value is a wait time given in
 * nanoseconds. It can either be relative, absolute monotonic, or
 * absolute adjustable depending on @a timeout_mode.
 *
 * Passing XN_INFINITE @b and setting @a timeout_mode to XN_RELATIVE
 * specifies an unbounded wait. All other values are used to
 * initialize a watchdog timer. If the current operation mode of the
 * system timer is oneshot and @a timeout elapses before
 * xnthread_suspend() has completed, then the target thread will not
 * be suspended, and this routine leads to a null effect.
 *
 * @param timeout_mode The mode of the @a timeout parameter. It can
 * either be set to XN_RELATIVE, XN_ABSOLUTE, or XN_REALTIME (see also
 * xntimer_start()).
 *
 * @param wchan The address of a pended resource. This parameter is
 * used internally by the synchronization object implementation code
 * to specify on which object the suspended thread pends. NULL is a
 * legitimate value when this parameter does not apply to the current
 * suspending mode (e.g. XNSUSP).
 *
 * @note If the target thread is a shadow which has received a
 * Linux-originated signal, then this service immediately exits
 * without suspending the thread, but raises the XNBREAK condition in
 * its information mask.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: possible if the current thread suspends itself.
 */
void xnthread_suspend(xnthread_t *thread, int mask,
		      xnticks_t timeout, xntmode_t timeout_mode,
		      xnsynch_t *wchan)
{
	unsigned long oldstate;
	struct xnsched *sched;
	spl_t s;

	/* No, you certainly do not want to suspend the root thread. */
	XENO_BUGON(NUCLEUS, xnthread_test_state(thread, XNROOT));
	/* No built-in support for conjunctive wait. */
	XENO_BUGON(NUCLEUS, wchan && thread->wchan);

	xnlock_get_irqsave(&nklock, s);

	trace_mark(xn_nucleus, thread_suspend,
		   "thread %p thread_name %s mask %lu timeout %Lu "
		   "timeout_mode %d wchan %p",
		   thread, xnthread_name(thread), mask, timeout,
		   timeout_mode, wchan);

	sched = thread->sched;
	oldstate = thread->state;

	if (thread == sched->curr)
		xnsched_set_resched(sched);

	/*
	 * If attempting to suspend a runnable thread which is pending
	 * a forced switch to secondary mode, just raise the break
	 * condition and return immediately.
	 *
	 * We may end up suspending a kicked thread that has been
	 * preempted on its relaxing path, which is a perfectly valid
	 * situation: we just ignore the signal notification in
	 * primary mode, and rely on the wakeup call pending for that
	 * task in the root context, to collect and act upon the
	 * pending Linux signal (see handle_sigwake_event()).
	 */
	if ((oldstate & XNTHREAD_BLOCK_BITS) == 0) {
		if ((mask & XNRELAX) == 0) {
			if (xnthread_test_info(thread, XNKICKED))
				goto abort;
			if (thread == sched->curr &&
			    (oldstate & (XNTRAPLB|XNLOCK)) == (XNTRAPLB|XNLOCK))
				goto abort;
		}
		xnthread_clear_info(thread,
				    XNRMID|XNTIMEO|XNBREAK|XNWAKEN|XNROBBED);
	}

	/*
	 * Don't start the timer for a thread delayed indefinitely.
	 */
	if (timeout != XN_INFINITE || timeout_mode != XN_RELATIVE) {
		xntimer_set_sched(&thread->rtimer, thread->sched);
		if (xntimer_start(&thread->rtimer, timeout, XN_INFINITE,
				  timeout_mode)) {
			/* (absolute) timeout value in the past, bail out. */
			if (wchan) {
				thread->wchan = wchan;
				xnsynch_forget_sleeper(thread);
			}
			xnthread_set_info(thread, XNTIMEO);
			goto out;
		}
		xnthread_set_state(thread, XNDELAY);
	}

	if (oldstate & XNREADY) {
		xnsched_dequeue(thread);
		xnthread_clear_state(thread, XNREADY);
	}

	xnthread_set_state(thread, mask);

	/*
	 * We must make sure that we don't clear the wait channel if a
	 * thread is first blocked (wchan != NULL) then forcibly
	 * suspended (wchan == NULL), since these are conjunctive
	 * conditions.
	 */
	if (wchan)
		thread->wchan = wchan;

	/*
	 * If the current thread is being relaxed, we must have been
	 * called from xnshadow_relax(), in which case we introduce an
	 * opportunity for interrupt delivery right before switching
	 * context, which shortens the uninterruptible code path.
	 *
	 * We have to shut irqs off before xnsched_run() though: if an
	 * interrupt could preempt us in __xnsched_run() right after
	 * the call to xnarch_escalate() but before we grab the
	 * nklock, we would enter the critical section in
	 * xnsched_run() while running in secondary mode, which would
	 * defeat the purpose of xnarch_escalate().
	 */
	if (likely(thread == sched->curr)) {
		sched->lflags &= ~XNINLOCK;
		if (unlikely(mask & XNRELAX)) {
			xnlock_clear_irqon(&nklock);
			splmax();
			xnsched_run();
			return;
		}
		/*
		 * If the thread is runnning on another CPU,
		 * xnsched_run will trigger the IPI as required.
		 */
		xnsched_run();
		goto out;
	}

	/*
	 * Ok, this one is an interesting corner case, which requires
	 * a bit of background first. Here, we handle the case of
	 * suspending a _relaxed_ user shadow which is _not_ the
	 * current thread.
	 *
	 *  The net effect is that we are attempting to stop the
	 * shadow thread at the nucleus level, whilst this thread is
	 * actually running some code under the control of the Linux
	 * scheduler (i.e. it's relaxed).
	 *
	 *  To make this possible, we force the target Linux task to
	 * migrate back to the Xenomai domain by sending it a
	 * SIGSHADOW signal the interface libraries trap for this
	 * specific internal purpose, whose handler is expected to
	 * call back the nucleus's migration service.
	 *
	 * By forcing this migration, we make sure that the real-time
	 * nucleus controls, hence properly stops, the target thread
	 * according to the requested suspension condition. Otherwise,
	 * the shadow thread in secondary mode would just keep running
	 * into the Linux domain, thus breaking the most common
	 * assumptions regarding suspended threads.
	 *
	 * We only care for threads that are not current, and for
	 * XNSUSP, XNDELAY, XNDORMANT and XNHELD conditions, because:
	 *
	 * - There is no point in dealing with relaxed threads, since
	 * personalities have to ask for primary mode switch when
	 * processing any syscall which may block the caller
	 * (i.e. __xn_exec_primary).
	 *
	 * - among all blocking bits (XNTHREAD_BLOCK_BITS), only
	 * XNSUSP, XNDELAY and XNHELD may be applied by the current
	 * thread to a non-current thread. XNPEND is always added by
	 * the caller to its own state, XNMIGRATE and XNRELAX have
	 * special semantics escaping this issue.
	 *
	 * We don't signal threads which are already in a dormant
	 * state, since they are suspended by definition.
	 */
	if (((oldstate & (XNTHREAD_BLOCK_BITS|XNUSER)) == (XNRELAX|XNUSER)) &&
	    (mask & (XNDELAY | XNSUSP | XNHELD)) != 0)
		xnshadow_send_sig(thread, SIGSHADOW, SIGSHADOW_ACTION_HARDEN);
out:
	xnlock_put_irqrestore(&nklock, s);
	return;

abort:
	if (wchan) {
		thread->wchan = wchan;
		xnsynch_forget_sleeper(thread);
	}
	xnthread_clear_info(thread, XNRMID | XNTIMEO);
	xnthread_set_info(thread, XNBREAK);
	xnlock_put_irqrestore(&nklock, s);
}
EXPORT_SYMBOL_GPL(xnthread_suspend);

/**
 * @fn void xnthread_resume(struct xnthread *thread,int mask)
 * @brief Resume a thread.
 *
 * Resumes the execution of a thread previously suspended by one or
 * more calls to xnthread_suspend(). This call removes a suspensive
 * condition affecting the target thread. When all suspensive
 * conditions are gone, the thread is left in a READY state at which
 * point it becomes eligible anew for scheduling.
 *
 * @param thread The descriptor address of the resumed thread.
 *
 * @param mask The suspension mask specifying the suspensive condition
 * to remove from the thread's wait mask. Possible values usable by
 * the caller are:
 *
 * - XNSUSP. This flag removes the explicit suspension condition. This
 * condition might be additive to the XNPEND condition.
 *
 * - XNDELAY. This flag removes the counted delay wait condition.
 *
 * - XNPEND. This flag removes the resource wait condition. If a
 * watchdog is armed, it is automatically disarmed by this
 * call. Unlike the two previous conditions, only the current thread
 * can set this condition for itself, i.e. no thread can force another
 * one to pend on a resource.
 *
 * When the thread is eventually resumed by one or more calls to
 * xnthread_resume(), the caller of xnthread_suspend() in the awakened
 * thread that suspended itself should check for the following bits in
 * its own information mask to determine what caused its wake up:
 *
 * - XNRMID means that the caller must assume that the pended
 * synchronization object has been destroyed (see xnsynch_flush()).
 *
 * - XNTIMEO means that the delay elapsed, or the watchdog went off
 * before the corresponding synchronization object was signaled.
 *
 * - XNBREAK means that the wait has been forcibly broken by a call to
 * xnthread_unblock().
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */
void xnthread_resume(struct xnthread *thread, int mask)
{
	unsigned long oldstate;
	struct xnsched *sched;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	trace_mark(xn_nucleus, thread_resume,
		   "thread %p thread_name %s mask %lu",
		   thread, xnthread_name(thread), mask);

	xntrace_pid(xnthread_host_pid(thread), xnthread_current_priority(thread));

	sched = thread->sched;
	oldstate = thread->state;

	if ((oldstate & XNTHREAD_BLOCK_BITS) == 0) {
		if (oldstate & XNREADY)
			xnsched_dequeue(thread);
		goto enqueue;
	}

	/* Clear the specified block bit(s) */
	xnthread_clear_state(thread, mask);

	/*
	 * If XNDELAY was set in the clear mask, xnthread_unblock()
	 * was called for the thread, or a timeout has elapsed. In the
	 * latter case, stopping the timer is a no-op.
	 */
	if (mask & XNDELAY)
		xntimer_stop(&thread->rtimer);

	if (!xnthread_test_state(thread, XNTHREAD_BLOCK_BITS))
		goto clear_wchan;

	if (mask & XNDELAY) {
		mask = xnthread_test_state(thread, XNPEND);
		if (mask == 0)
			goto unlock_and_exit;
		if (thread->wchan)
			xnsynch_forget_sleeper(thread);
		goto recheck_state;
	}

	if (xnthread_test_state(thread, XNDELAY)) {
		if (mask & XNPEND) {
			/*
			 * A resource became available to the thread.
			 * Cancel the watchdog timer.
			 */
			xntimer_stop(&thread->rtimer);
			xnthread_clear_state(thread, XNDELAY);
		}
		goto recheck_state;
	}

	/*
	 * The thread is still suspended, but is no more pending on a
	 * resource.
	 */
	if ((mask & XNPEND) != 0 && thread->wchan)
		xnsynch_forget_sleeper(thread);

	goto unlock_and_exit;

recheck_state:
	if (xnthread_test_state(thread, XNTHREAD_BLOCK_BITS))
		goto unlock_and_exit;

clear_wchan:
	if ((mask & ~XNDELAY) != 0 && thread->wchan != NULL)
		/*
		 * If the thread was actually suspended, clear the
		 * wait channel.  -- this allows requests like
		 * xnthread_suspend(thread,XNDELAY,...)  not to run
		 * the following code when the suspended thread is
		 * woken up while undergoing a simple delay.
		 */
		xnsynch_forget_sleeper(thread);

	if (unlikely((oldstate & mask) & XNHELD)) {
		xnsched_requeue(thread);
		goto ready;
	}
enqueue:
	xnsched_enqueue(thread);
ready:
	xnthread_set_state(thread, XNREADY);
	xnsched_set_resched(sched);
unlock_and_exit:
	xnlock_put_irqrestore(&nklock, s);
}
EXPORT_SYMBOL_GPL(xnthread_resume);

/**
 * @fn int xnthread_unblock(xnthread_t *thread)
 * @brief Unblock a thread.
 *
 * Breaks the thread out of any wait it is currently in.  This call
 * removes the XNDELAY and XNPEND suspensive conditions previously put
 * by xnthread_suspend() on the target thread. If all suspensive
 * conditions are gone, the thread is left in a READY state at which
 * point it becomes eligible anew for scheduling.
 *
 * @param thread The descriptor address of the unblocked thread.
 *
 * This call neither releases the thread from the XNSUSP, XNRELAX,
 * XNDORMANT or XNHELD suspensive conditions.
 *
 * When the thread resumes execution, the XNBREAK bit is set in the
 * unblocked thread's information mask. Unblocking a non-blocked
 * thread is perfectly harmless.
 *
 * @return non-zero is returned if the thread was actually unblocked
 * from a pending wait state, 0 otherwise.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */
int xnthread_unblock(xnthread_t *thread)
{
	int ret = 1;
	spl_t s;

	/*
	 * Attempt to abort an undergoing wait for the given thread.
	 * If this state is due to an alarm that has been armed to
	 * limit the sleeping thread's waiting time while it pends for
	 * a resource, the corresponding XNPEND state will be cleared
	 * by xnthread_resume() in the same move. Otherwise, this call
	 * may abort an undergoing infinite wait for a resource (if
	 * any).
	 */
	xnlock_get_irqsave(&nklock, s);

	trace_mark(xn_nucleus, thread_unblock,
		   "thread %p thread_name %s state %lu",
		   thread, xnthread_name(thread),
		   xnthread_state_flags(thread));

	if (xnthread_test_state(thread, XNDELAY))
		xnthread_resume(thread, XNDELAY);
	else if (xnthread_test_state(thread, XNPEND))
		xnthread_resume(thread, XNPEND);
	else
		ret = 0;

	/*
	 * We should not clear a previous break state if this service
	 * is called more than once before the target thread actually
	 * resumes, so we only set the bit here and never clear
	 * it. However, we must not raise the XNBREAK bit if the
	 * target thread was already awake at the time of this call,
	 * so that downstream code does not get confused by some
	 * "successful but interrupted syscall" condition. IOW, a
	 * break state raised here must always trigger an error code
	 * downstream, and an already successful syscall cannot be
	 * marked as interrupted.
	 */
	if (ret)
		xnthread_set_info(thread, XNBREAK);

	xnlock_put_irqrestore(&nklock, s);

	return ret;
}
EXPORT_SYMBOL_GPL(xnthread_unblock);

/**
 * @fn int xnthread_set_periodic(xnthread_t *thread,xnticks_t idate, xntmode_t timeout_mode, xnticks_t period)
 * @brief Make a thread periodic.
 *
 * Make a thread periodic by programming its first release point and
 * its period in the processor time line.  Subsequent calls to
 * xnthread_wait_period() will delay the thread until the next
 * periodic release point in the processor timeline is reached.
 *
 * @param thread The descriptor address of the affected thread. This
 * thread is immediately delayed until the first periodic release
 * point is reached.
 *
 * @param idate The initial (absolute) date of the first release
 * point, expressed in nanoseconds. The affected thread will be
 * delayed by the first call to xnthread_wait_period() until this
 * point is reached. If @a idate is equal to XN_INFINITE, the current
 * system date is used, and no initial delay takes place. In the
 * latter case, @a timeout_mode is not considered and can have any
 * valid value.
 *
 * @param timeout_mode The mode of the @a idate parameter. It can
 * either be set to XN_ABSOLUTE or XN_REALTIME with @a idate different
 * from XN_INFINITE (see also xntimer_start()).
 *
 * @param period The period of the thread, expressed in nanoseconds.
 * As a side-effect, passing XN_INFINITE attempts to stop the thread's
 * periodic timer; in the latter case, the routine always exits
 * succesfully, regardless of the previous state of this timer.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -ETIMEDOUT is returned @a idate is different from XN_INFINITE and
 * represents a date in the past.
 *
 * - -EINVAL is returned if @a period is different from XN_INFINITE
 * but shorter than the scheduling latency value for the target
 * system, as available from /proc/xenomai/latency. -EINVAL is also
 * returned if @a timeout_mode is not compatible with @a idate, such
 * as XN_RELATIVE with @a idate different from XN_INFINITE.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: none.
 */
int xnthread_set_periodic(xnthread_t *thread, xnticks_t idate,
			  xntmode_t timeout_mode, xnticks_t period)
{
	int ret = 0;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	trace_mark(xn_nucleus, thread_setperiodic,
		   "thread %p thread_name %s idate %Lu mode %d period %Lu timer %p",
		   thread, xnthread_name(thread), idate, timeout_mode, period,
		   &thread->ptimer);

	if (period == XN_INFINITE) {
		if (xntimer_running_p(&thread->ptimer))
			xntimer_stop(&thread->ptimer);

		goto unlock_and_exit;
	}

	if (period < xnclock_ticks_to_ns(&nkclock, nkclock.gravity)) {
		/*
		 * LART: detect periods which are shorter than the
		 * clock gravity. This can't work, caller must have
		 * messed up with arguments.
		 */
		ret = -EINVAL;
		goto unlock_and_exit;
	}

	xntimer_set_sched(&thread->ptimer, thread->sched);

	if (idate == XN_INFINITE)
		xntimer_start(&thread->ptimer, period, period, XN_RELATIVE);
	else {
		if (timeout_mode == XN_REALTIME)
			idate -= xnclock_get_offset(&nkclock);
		else if (timeout_mode != XN_ABSOLUTE) {
			ret = -EINVAL;
			goto unlock_and_exit;
		}
		ret = xntimer_start(&thread->ptimer, idate + period, period,
				    XN_ABSOLUTE);
	}

unlock_and_exit:
	xnlock_put_irqrestore(&nklock, s);

	return ret;
}
EXPORT_SYMBOL_GPL(xnthread_set_periodic);

/**
 * @fn int xnthread_wait_period(unsigned long *overruns_r)
 * @brief Wait for the next periodic release point.
 *
 * Make the current thread wait for the next periodic release point in
 * the processor time line.
 *
 * @param overruns_r If non-NULL, @a overruns_r must be a pointer to a
 * memory location which will be written with the count of pending
 * overruns. This value is copied only when xnthread_wait_period()
 * returns -ETIMEDOUT or success; the memory location remains
 * unmodified otherwise. If NULL, this count will never be copied
 * back.
 *
 * @return 0 is returned upon success; if @a overruns_r is valid, zero
 * is copied to the pointed memory location. Otherwise:
 *
 * - -EWOULDBLOCK is returned if xnthread_set_periodic() has not
 * previously been called for the calling thread.
 *
 * - -EINTR is returned if xnthread_unblock() has been called for the
 * waiting thread before the next periodic release point has been
 * reached. In this case, the overrun counter is reset too.
 *
 * - -ETIMEDOUT is returned if the timer has overrun, which indicates
 * that one or more previous release points have been missed by the
 * calling thread. If @a overruns_r is valid, the count of pending
 * overruns is copied to the pointed memory location.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: always, unless the current release point has already
 * been reached.  In the latter case, the current thread immediately
 * returns from this service without being delayed.
 */
int xnthread_wait_period(unsigned long *overruns_r)
{
	unsigned long overruns = 0;
	xnthread_t *thread;
	xnticks_t now;
	int err = 0;
	spl_t s;

	thread = xnsched_current_thread();

	xnlock_get_irqsave(&nklock, s);

	if (unlikely(!xntimer_running_p(&thread->ptimer))) {
		err = -EWOULDBLOCK;
		goto unlock_and_exit;
	}

	trace_mark(xn_nucleus, thread_waitperiod, "thread %p thread_name %s",
		   thread, xnthread_name(thread));

	now = xnclock_read_raw(&nkclock);
	if (likely((xnsticks_t)(now - xntimer_pexpect(&thread->ptimer)) < 0)) {
		xnthread_suspend(thread, XNDELAY, XN_INFINITE, XN_RELATIVE, NULL);
		if (unlikely(xnthread_test_info(thread, XNBREAK))) {
			err = -EINTR;
			goto unlock_and_exit;
		}

		now = xnclock_read_raw(&nkclock);
	}

	overruns = xntimer_get_overruns(&thread->ptimer, now);
	if (overruns) {
		err = -ETIMEDOUT;

		trace_mark(xn_nucleus, thread_missedperiod,
			   "thread %p thread_name %s overruns %lu",
			   thread, xnthread_name(thread), overruns);
	}

	if (likely(overruns_r != NULL))
		*overruns_r = overruns;

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}
EXPORT_SYMBOL_GPL(xnthread_wait_period);

/**
 * @fn int xnthread_set_slice(struct xnthread *thread, xnticks_t quantum)
 * @brief Set thread time-slicing information.
 *
 * Update the time-slicing information for a given thread. This
 * service enables or disables round-robin scheduling for the thread,
 * depending on the value of @a quantum. By default, times-slicing is
 * disabled for a new thread initialized by a call to xnthread_init().
 *
 * @param thread The descriptor address of the affected thread.
 *
 * @param quantum The time quantum assigned to the thread expressed in
 * nanoseconds. If @a quantum is different from XN_INFINITE, the
 * time-slice for the thread is set to that value and its current time
 * credit is refilled (i.e. the thread is given a full time-slice to
 * run next). Otherwise, if @a quantum equals XN_INFINITE,
 * time-slicing is stopped for that thread.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a quantum is not XN_INFINITE, and the
 * base scheduling class of the target thread does not support
 * time-slicing.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Any kernel context.
 *
 * Rescheduling: never.
 */
int xnthread_set_slice(struct xnthread *thread, xnticks_t quantum)
{
	struct xnsched *sched;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	sched = thread->sched;
	thread->rrperiod = quantum;

	if (quantum != XN_INFINITE) {
		if (thread->base_class->sched_tick == NULL) {
			xnlock_put_irqrestore(&nklock, s);
			return -EINVAL;
		}
		xnthread_set_state(thread, XNRRB);
		if (sched->curr == thread)
			xntimer_start(&sched->rrbtimer,
				      quantum, XN_INFINITE, XN_RELATIVE);
	} else {
		xnthread_clear_state(thread, XNRRB);
		if (sched->curr == thread)
			xntimer_stop(&sched->rrbtimer);
	}

	xnlock_put_irqrestore(&nklock, s);

	return 0;
}
EXPORT_SYMBOL_GPL(xnthread_set_slice);

/**
 * @fn void xnthread_cancel(struct xnthread *thread)
 * @brief Cancel a thread.
 *
 * Request cancellation of a thread. This service forces @a thread to
 * exit from any blocking call. @a thread will terminate as soon as it
 * reaches a cancellation point. Cancellation points are defined for
 * the following situations:
 *
 * - @a thread self-cancels by a call to xnthread_cancel().
 * - @a thread invokes a Linux syscall (user-space shadow only).
 * - @a thread receives a Linux signal (user-space shadow only).
 * - @a thread explicitly calls xnthread_test_cancel().
 *
 * @param thread The descriptor address of the thread to terminate.
 *
 * Calling context: This service may be called from all runtime modes.
 *
 * Rescheduling: yes.
 */
void xnthread_cancel(struct xnthread *thread)
{
	spl_t s;

	/* Right, so you want to kill the kernel?! */
	XENO_BUGON(NUCLEUS, xnthread_test_state(thread, XNROOT));

	xnlock_get_irqsave(&nklock, s);

	if (xnthread_test_info(thread, XNCANCELD))
		goto check_self_cancel;

	trace_mark(xn_nucleus, thread_cancel, "thread %p thread_name %s",
		   thread, xnthread_name(thread));

	xnthread_set_info(thread, XNCANCELD);

	/*
	 * If @thread is not started yet, fake a start request,
	 * raising the kicked condition bit to make sure it will reach
	 * xnthread_test_cancel() on its wakeup path.
	 */
	if (xnthread_test_state(thread, XNDORMANT)) {
		xnthread_set_info(thread, XNKICKED);
		xnthread_resume(thread, XNDORMANT);
		xnsched_run();
		goto unlock_and_exit;
	}

check_self_cancel:
	if (xnshadow_current() == thread) {
		xnlock_put_irqrestore(&nklock, s);
		xnthread_test_cancel();
		/*
		 * May return if on behalf of an IRQ handler which has
		 * preempted @thread.
		 */
		return;
	}

	__xnshadow_kick(thread);
	xnsched_run();

unlock_and_exit:
	xnlock_put_irqrestore(&nklock, s);
}
EXPORT_SYMBOL_GPL(xnthread_cancel);

/**
 * @fn void xnthread_join(struct xnthread *thread)
 * @brief Join with a terminated thread.
 *
 * This service waits for @a thread to terminate after a call to
 * xnthread_cancel().  If that thread has already terminated or is
 * dormant at the time of the call, then xnthread_join() returns
 * immediately.
 *
 * xnthread_join() adapts to the calling context (primary or
 * secondary).
 *
 * @param thread The descriptor address of the thread to join with.
 *
 * @return 0 is returned on success. Otherwise, the following error
 * codes indicate the cause of the failure:
 *
 * - -EDEADLK is returned if the current thread attempts to join
 * itself.
 *
 * - -EINTR is returned if the current thread was unblocked while
 *   waiting for @a thread to terminate.
 *
 * - -EBUSY indicates that another thread is already waiting for @a
 *   thread to terminate.
 *
 * Calling context: any.
 *
 * Rescheduling: always if @a thread did not terminate yet at the time
 * of the call.
 */
int xnthread_join(struct xnthread *thread)
{
	unsigned int tag;
	spl_t s;
	int ret;

	XENO_BUGON(NUCLEUS, xnthread_test_state(thread, XNROOT));

	xnlock_get_irqsave(&nklock, s);

	tag = thread->idtag;
	if (xnthread_test_info(thread, XNDORMANT) || tag == 0) {
		xnlock_put_irqrestore(&nklock, s);
		return 0;
	}

	trace_mark(xn_nucleus, thread_join, "thread %p thread_name %s",
		   thread, xnthread_name(thread));

	if (ipipe_root_p) {
		if (xnthread_test_state(thread, XNJOINED)) {
			ret = -EBUSY;
			goto out;
		}
		xnthread_set_state(thread, XNJOINED);
		xnlock_put_irqrestore(&nklock, s);
		/*
		 * Only a very few threads are likely to terminate within a
		 * short time frame at any point in time, so experiencing a
		 * thundering herd effect due to synchronizing on a single
		 * wait queue is quite unlikely. In any case, we run in
		 * secondary mode.
		 */
		if (wait_event_interruptible(nkjoinq, thread->idtag != tag)) {
			xnlock_get_irqsave(&nklock, s);
			if (thread->idtag == tag)
				xnthread_clear_state(thread, XNJOINED);
			ret = -EINTR;
			goto out;
		}

		return 0;
	}

	if (thread == xnsched_current_thread())
		ret = -EDEADLK;
	else if (xnsynch_pended_p(&thread->join_synch))
		ret = -EBUSY;
	else {
		xnthread_set_state(thread, XNJOINED);
		ret = xnsynch_sleep_on(&thread->join_synch,
				       XN_INFINITE, XN_RELATIVE);
		if ((ret & XNRMID) == 0 && thread->idtag == tag)
			xnthread_clear_state(thread, XNJOINED);
		ret = ret & XNBREAK ? -EINTR : 0;
	}
out:
	xnlock_put_irqrestore(&nklock, s);

	return ret;
}
EXPORT_SYMBOL_GPL(xnthread_join);

/**
 * @fn int xnthread_migrate(int cpu)
 * @brief Migrate the current thread.
 *
 * This call makes the current thread migrate to another (real-time)
 * CPU if its affinity allows it. This call is available from
 * primary mode only.
 *
 * @param cpu The destination CPU.
 *
 * @retval 0 if the thread could migrate ;
 * @retval -EPERM if the calling context is invalid, or the
 * scheduler is locked.
 * @retval -EINVAL if the current thread affinity forbids this
 * migration.
 */

#ifdef CONFIG_SMP

int xnthread_migrate(int cpu)
{
	struct xnthread *thread;
	struct xnsched *sched;
	int ret = 0;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (!xnsched_primary_p() || xnsched_locked_p()) {
		ret = -EPERM;
		goto unlock_and_exit;
	}

	thread = xnsched_current_thread();
	if (!cpu_isset(cpu, thread->affinity)) {
		ret = -EINVAL;
		goto unlock_and_exit;
	}

	sched = xnsched_struct(cpu);
	if (sched == xnthread_sched(thread))
		goto unlock_and_exit;

	trace_mark(xn_nucleus, thread_migrate,
		   "thread %p thread_name %s cpu %d",
		   thread, xnthread_name(thread), cpu);

	/* Move to remote scheduler. */
	xnsched_migrate(thread, sched);

	/*
	 * Migrate the thread's periodic timer. We don't have to care
	 * about the resource timer, since we can only deal with the
	 * current thread, which is, well, running, so it can't be
	 * sleeping on any timed wait at the moment.
	 */
	__xntimer_migrate(&thread->ptimer, sched);

	/*
	 * Reset execution time measurement period so that we don't
	 * mess up per-CPU statistics.
	 */
	xnstat_exectime_reset_stats(&thread->stat.lastperiod);

	/*
	 * So that xnshadow_relax() will pin the linux mate on the
	 * same CPU next time the thread switches to secondary mode.
	 */
	xnthread_set_info(thread, XNMOVED);

	xnsched_run();

 unlock_and_exit:
	xnlock_put_irqrestore(&nklock, s);

	return ret;
}
EXPORT_SYMBOL_GPL(xnthread_migrate);

void xnthread_migrate_passive(struct xnthread *thread, struct xnsched *sched)
{				/* nklocked, IRQs off */
	trace_mark(xn_nucleus, thread_migrate_passive,
		   "thread %p thread_name %s cpu %d",
		   thread, xnthread_name(thread), xnsched_cpu(sched));

	XENO_BUGON(NUCLEUS, !cpu_isset(xnsched_cpu(sched), xnsched_realtime_cpus));

	if (thread->sched == sched)
		return;
	/*
	 * Timer migration is postponed until the next timeout happens
	 * for the periodic and rrb timers. The resource timer will be
	 * moved to the right CPU next time it is armed in
	 * xnthread_suspend().
	 */
	xnsched_migrate_passive(thread, sched);

	xnstat_exectime_reset_stats(&thread->stat.lastperiod);
}

#endif	/* CONFIG_SMP */

/**
 * @fn int xnthread_set_schedparam(struct xnthread *thread,struct xnsched_class *sched_class,const union xnsched_policy_param *sched_param)
 * @brief Change the base scheduling parameters of a thread.
 *
 * Changes the base scheduling policy and paramaters of a thread. If
 * the thread is currently blocked, waiting in priority-pending mode
 * (XNSYNCH_PRIO) for a synchronization object to be signaled, the
 * nucleus will attempt to reorder the object's wait queue so that it
 * reflects the new sleeper's priority, unless the XNSYNCH_DREORD flag
 * has been set for the pended object.
 *
 * @param thread The descriptor address of the affected thread. See
 * note.
 *
 * @param sched_class The new scheduling class the thread should be
 * assigned to.
 *
 * @param sched_param The scheduling parameters to set for the thread;
 * @a sched_param must be valid within the context of @a sched_class.
 *
 * It is absolutely required to use this service to change a thread
 * priority, in order to have all the needed housekeeping chores
 * correctly performed. i.e. Do *not* call xnsched_set_policy()
 * directly or worse, change the thread.cprio field by hand in any
 * case.
 *
 * @return 0 is returned on success. Otherwise, a negative error code
 * indicates the cause of a failure that happened in the scheduling
 * class implementation for @a sched_class. Invalid parameters passed
 * into @a sched_param are common causes of error.
 *
 * Side-effects:
 *
 * - This service does not call the rescheduling procedure but may
 * affect the state of the runnable queue for the previous and new
 * scheduling classes.
 *
 * - Assigning the same scheduling class and parameters to a running
 * or ready thread moves it to the end of the runnable queue, thus
 * causing a manual round-robin.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Primary mode only.
 *
 * Rescheduling: never.
 *
 * @note The changes only apply to the Xenomai scheduling parameters
 * for @a thread. There is no propagation/translation of such changes
 * to the Linux scheduler for the task mated to the Xenomai target
 * thread.
 */
int xnthread_set_schedparam(struct xnthread *thread,
			    struct xnsched_class *sched_class,
			    const union xnsched_policy_param *sched_param)
{
	int old_wprio, new_wprio, ret;
	spl_t s;

	primary_mode_only();

	xnlock_get_irqsave(&nklock, s);

	old_wprio = thread->wprio;

	ret = xnsched_set_policy(thread, sched_class, sched_param);
	if (ret)
		goto unlock_and_exit;

	new_wprio = thread->wprio;

	trace_mark(xn_nucleus, set_thread_schedparam,
		   "thread %p thread_name %s class %s prio %d",
		   thread, xnthread_name(thread),
		   thread->sched_class->name, thread->cprio);
	/*
	 * NOTE: The behaviour changed compared to v2.4.x: we do not
	 * prevent the caller from altering the scheduling parameters
	 * of a thread that currently undergoes a PIP boost
	 * anymore. Rationale: Calling xnthread_set_schedparam()
	 * carelessly with no consideration for resource management is
	 * a bug in essence, and xnthread_set_schedparam() does not
	 * have to paper over it, especially at the cost of more
	 * complexity when dealing with multiple scheduling classes.
	 * In short, callers have to make sure that lowering a thread
	 * priority is safe with respect to what their application
	 * currently does.
	 */
	if (old_wprio != new_wprio && thread->wchan != NULL &&
	    (thread->wchan->status & XNSYNCH_DREORD) == 0)
		/*
		 * Update the pending order of the thread inside its
		 * wait queue, unless this behaviour has been
		 * explicitly disabled for the pended synchronization
		 * object, or the requested (weighted) priority has
		 * not changed, thus preventing spurious round-robin
		 * effects.
		 */
		xnsynch_requeue_sleeper(thread);
	/*
	 * We don't need/want to move the thread at the end of its
	 * priority group whenever:
	 * - it is blocked and thus not runnable;
	 * - it bears the ready bit in which case xnsched_set_policy()
	 * already reordered the runnable queue;
	 * - we currently hold the scheduler lock, so we don't want
	 * any round-robin effect to take place.
	 */
	if (!xnthread_test_state(thread, XNTHREAD_BLOCK_BITS|XNREADY|XNLOCK))
		xnsched_putback(thread);

unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return ret;
}
EXPORT_SYMBOL_GPL(xnthread_set_schedparam);

void __xnthread_test_cancel(struct xnthread *curr)
{
	/*
	 * Just in case xnthread_test_cancel() is called from an IRQ
	 * handler, in which case we may not take the exit path.
	 *
	 * NOTE: curr->sched is stable from our POV and can't change
	 * under our feet.
	 */
	if (curr->sched->lflags & XNINIRQ)
		return;

	if (!xnthread_test_state(curr, XNRELAX))
		xnshadow_relax(0, 0);

	do_exit(0);
	/* ... won't return ... */
	XENO_BUGON(NUCLEUS, 1);
}
EXPORT_SYMBOL_GPL(__xnthread_test_cancel);

/*@}*/

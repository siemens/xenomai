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

#include <linux/kthread.h>
#include <cobalt/kernel/pod.h>
#include <cobalt/kernel/synch.h>
#include <cobalt/kernel/heap.h>
#include <cobalt/kernel/thread.h>
#include <cobalt/kernel/sched.h>
#include <cobalt/kernel/clock.h>
#include <cobalt/kernel/shadow.h>
#include <asm/xenomai/thread.h>

static unsigned int idtags;

static void timeout_handler(xntimer_t *timer)
{
	struct xnthread *thread = container_of(timer, xnthread_t, rtimer);

	xnthread_set_info(thread, XNTIMEO);	/* Interrupts are off. */
	xnpod_resume_thread(thread, XNDELAY);
}

static void periodic_handler(xntimer_t *timer)
{
	struct xnthread *thread = container_of(timer, xnthread_t, ptimer);
	/*
	 * Prevent unwanted round-robin, and do not wake up threads
	 * blocked on a resource.
	 */
	if (xnthread_test_state(thread, XNDELAY|XNPEND) == XNDELAY)
		xnpod_resume_thread(thread, XNDELAY);
}

static void roundrobin_handler(xntimer_t *timer)
{
	struct xnthread *thread = container_of(timer, struct xnthread, rrbtimer);
	xnsched_tick(thread);
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

	xnpod_cancel_thread(thread);

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

int xnthread_init(struct xnthread *thread,
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

	xntimer_init(&thread->rtimer, timeout_handler);
	xntimer_set_name(&thread->rtimer, thread->name);
	xntimer_set_priority(&thread->rtimer, XNTIMER_HIPRIO);
	xntimer_init(&thread->ptimer, periodic_handler);
	xntimer_set_name(&thread->ptimer, thread->name);
	xntimer_set_priority(&thread->ptimer, XNTIMER_HIPRIO);
	xntimer_init(&thread->rrbtimer, roundrobin_handler);
	xntimer_set_name(&thread->rrbtimer, thread->name);
	xntimer_set_priority(&thread->rrbtimer, XNTIMER_LOPRIO);

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

	/* These will be filled by xnpod_start_thread() */
	thread->imode = 0;
	thread->entry = NULL;
	thread->cookie = NULL;

	thread->selector = NULL;
	INIT_LIST_HEAD(&thread->claimq);

	thread->privdata = NULL;
	thread->personality = attr->personality;

	thread->sched = sched;
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

void xnthread_cleanup(struct xnthread *thread)
{
	/* Does not wreck the TCB, only releases the held resources. */

	if (thread->registry.handle != XN_NO_HANDLE)
		xnregistry_remove(thread->registry.handle);

	thread->registry.handle = XN_NO_HANDLE;
}

char *xnthread_format_status(unsigned long status, char *buf, int size)
{
	static const char labels[] = XNTHREAD_STATE_LABELS;
	int pos, c, mask;
	char *wp;

	for (mask = (int)status & ~XNTHREAD_STATE_SPARES, pos = 0, wp = buf;
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

xnticks_t xnthread_get_timeout(xnthread_t *thread, xnticks_t tsc_ns)
{
	xnticks_t timeout;
	xntimer_t *timer;

	if (!xnthread_test_state(thread,XNDELAY))
		return 0LL;

	if (xntimer_running_p(&thread->rtimer))
		timer = &thread->rtimer;
	else if (xntimer_running_p(&thread->ptimer))
		timer = &thread->ptimer;
	else
		return 0LL;

	timeout = xntimer_get_date(timer);
	if (timeout <= tsc_ns)
		return 1;

	return timeout - tsc_ns;
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

/* NOTE: caller must provide for locking */
void xnthread_prepare_wait(struct xnthread_wait_context *wc)
{
	struct xnthread *curr = xnpod_current_thread();

	curr->wcontext = wc;
}
EXPORT_SYMBOL_GPL(xnthread_prepare_wait);

/* NOTE: caller must provide for locking */
void xnthread_finish_wait(struct xnthread_wait_context *wc,
			  void (*cleanup)(struct xnthread_wait_context *wc))
{
	struct xnthread *curr = xnpod_current_thread();

	curr->wcontext = NULL;

	if (xnthread_test_info(curr, XNCANCELD)) {
		if (cleanup)
			cleanup(wc);
		xnpod_cancel_thread(curr);
	}
}
EXPORT_SYMBOL_GPL(xnthread_finish_wait);

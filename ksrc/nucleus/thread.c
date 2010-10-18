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

#include <nucleus/pod.h>
#include <nucleus/synch.h>
#include <nucleus/heap.h>
#include <nucleus/thread.h>
#include <nucleus/module.h>
#include <asm/xenomai/bits/thread.h>

static unsigned idtags;

static void xnthread_timeout_handler(xntimer_t *timer)
{
	xnthread_t *thread = container_of(timer, xnthread_t, rtimer);
	xnthread_set_info(thread, XNTIMEO);	/* Interrupts are off. */
	xnpod_resume_thread(thread, XNDELAY);
}

static void xnthread_periodic_handler(xntimer_t *timer)
{
	xnthread_t *thread = container_of(timer, xnthread_t, ptimer);
	/*
	 * Prevent unwanted round-robin, and do not wake up threads
	 * blocked on a resource.
	 */
	if (xnthread_test_state(thread, XNDELAY|XNPEND) == XNDELAY)
		xnpod_resume_thread(thread, XNDELAY);
}

int xnthread_init(struct xnthread *thread,
		  const struct xnthread_init_attr *attr,
		  struct xnsched *sched,
		  struct xnsched_class *sched_class,
		  const union xnsched_policy_param *sched_param)
{
	unsigned int stacksize = attr->stacksize;
	xnflags_t flags = attr->flags;
	struct xnarchtcb *tcb;
	int ret;

	/* Setup the TCB. */
	tcb = xnthread_archtcb(thread);
	xnarch_init_tcb(tcb);

	flags &= ~XNSUSP;
#ifndef CONFIG_XENO_HW_FPU
	flags &= ~XNFPU;
#endif
#ifdef __XENO_SIM__
	flags &= ~XNSHADOW;
#endif
	if (flags & (XNSHADOW|XNROOT))
		stacksize = 0;
	else {
		if (stacksize == 0) /* Pick a reasonable default. */
			stacksize = XNARCH_THREAD_STACKSZ;
		/* Align stack size on a natural word boundary */
		stacksize &= ~(sizeof(long) - 1);
	}

	if (flags & XNROOT)
		thread->idtag = 0;
	else
		thread->idtag = ++idtags ?: 1;

#if CONFIG_XENO_OPT_SYS_STACKPOOLSZ == 0
#ifndef __XENO_SIM__
	if (stacksize > 0) {
		xnlogerr("%s: cannot create kernel thread '%s' (CONFIG_XENO_OPT_SYS_STACKPOOLSZ == 0)\n",
			 __FUNCTION__, attr->name);
		return -ENOMEM;
	}
#endif
#else
	ret = xnarch_alloc_stack(tcb, stacksize);
	if (ret) {
		xnlogerr("%s: no stack for kernel thread '%s' (raise CONFIG_XENO_OPT_SYS_STACKPOOLSZ)\n",
			 __FUNCTION__, attr->name);
		return ret;
	}
#endif
	if (stacksize)
		memset(xnarch_stack_base(tcb), 0, stacksize);

	if (attr->name)
		xnobject_copy_name(thread->name, attr->name);
	else
		snprintf(thread->name, sizeof(thread->name), "%p", thread);

	xntimer_init(&thread->rtimer, attr->tbase, xnthread_timeout_handler);
	xntimer_set_name(&thread->rtimer, thread->name);
	xntimer_set_priority(&thread->rtimer, XNTIMER_HIPRIO);
	xntimer_init(&thread->ptimer, attr->tbase, xnthread_periodic_handler);
	xntimer_set_name(&thread->ptimer, thread->name);
	xntimer_set_priority(&thread->ptimer, XNTIMER_HIPRIO);

	thread->state = flags;
	thread->info = 0;
	thread->schedlck = 0;
	thread->signals = 0;
	thread->asrmode = 0;
	thread->asrimask = 0;
	thread->asr = XNTHREAD_INVALID_ASR;
	thread->asrlevel = 0;

	thread->ops = attr->ops;
	thread->rrperiod = XN_INFINITE;
	thread->rrcredit = XN_INFINITE;
	thread->wchan = NULL;
	thread->wwake = NULL;
	thread->wcontext = NULL;
	thread->hrescnt = 0;
	thread->errcode = 0;
	thread->registry.handle = XN_NO_HANDLE;
	thread->registry.waitkey = NULL;
	memset(&thread->stat, 0, sizeof(thread->stat));

	/* These will be filled by xnpod_start_thread() */
	thread->imask = 0;
	thread->imode = 0;
	thread->entry = NULL;
	thread->cookie = 0;

	inith(&thread->glink);
	initph(&thread->rlink);
	initph(&thread->plink);
#ifdef CONFIG_XENO_OPT_PRIOCPL
	initph(&thread->xlink);
	thread->rpi = NULL;
#endif /* CONFIG_XENO_OPT_PRIOCPL */
#ifdef CONFIG_XENO_OPT_SELECT
	thread->selector = NULL;
#endif /* CONFIG_XENO_OPT_SELECT */
	initpq(&thread->claimq);

	thread->sched = sched;
	thread->init_class = sched_class;
	thread->base_class = NULL; /* xnsched_set_policy() will set it. */
	thread->init_schedparam = *sched_param;
	ret = xnsched_init_tcb(thread);
	if (ret)
		goto fail;

	/*
	 * We must set the scheduling policy last; the scheduling
	 * class implementation code may need the TCB to be fully
	 * initialized to proceed.
	 */
	ret = xnsched_set_policy(thread, sched_class, sched_param);
	if (ret)
		goto fail;

	xnarch_init_display_context(thread);

	return 0;

fail:
#if CONFIG_XENO_OPT_SYS_STACKPOOLSZ > 0
	xnarch_free_stack(tcb);
#endif
	return ret;
}

void xnthread_cleanup_tcb(xnthread_t *thread)
{
	/* Does not wreck the TCB, only releases the held resources. */

#if CONFIG_XENO_OPT_SYS_STACKPOOLSZ > 0
	xnarch_free_stack(xnthread_archtcb(thread));
#endif
	if (thread->registry.handle != XN_NO_HANDLE)
		xnregistry_remove(thread->registry.handle);

	thread->registry.handle = XN_NO_HANDLE;
}

char *xnthread_format_status(xnflags_t status, char *buf, int size)
{
	static const char labels[] = XNTHREAD_STATE_LABELS;
	xnflags_t mask;
	int pos, c;
	char *wp;

	for (mask = status & ~XNTHREAD_STATE_SPARES, pos = 0, wp = buf;
	     mask != 0 && wp - buf < size - 2;	/* 1-letter label + \0 */
	     mask >>= 1, pos++) {
		if ((mask & 1) == 0)
			continue;

		c = labels[pos];

		switch (1 << pos) {
		case XNFPU:
			/*
			 * Only output the FPU flag for kernel-based
			 * threads; Others get the same level of fp
			 * support than any user-space tasks on the
			 * current platform.
			 */
			if (status & (XNSHADOW | XNROOT))
				continue;
			break;
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

int *xnthread_get_errno_location(xnthread_t *thread)
{
	static int fallback_errno;

	if (unlikely(!xnpod_active_p()))
		return &fallback_errno;

#ifdef CONFIG_XENO_OPT_PERVASIVE
	if (xnthread_test_state(thread, XNSHADOW))
		return &thread->errcode;

	if (xnthread_test_state(thread, XNROOT))
		return &xnshadow_errno(current);
#endif /* CONFIG_XENO_OPT_PERVASIVE */

	return &thread->errcode;
}
EXPORT_SYMBOL_GPL(xnthread_get_errno_location);

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
	/*
	 * The caller should have masked IRQs while collecting the
	 * timeout(s), so no tick could be announced in the meantime,
	 * and all timeouts would always use the same epoch
	 * value. Obviously, this can't be a valid assumption for
	 * aperiodic timers, which values are based on the hardware
	 * TSC, and as such the current time will change regardless of
	 * the interrupt state; for this reason, we use the "tsc_ns"
	 * input parameter (TSC converted to nanoseconds) the caller
	 * has passed us as the epoch value instead.
	 */
	if (xntbase_periodic_p(xnthread_time_base(thread)))
		return xntimer_get_timeout(timer);

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

/* NOTE: caller must provide locking */
void xnthread_prepare_wait(struct xnthread_wait_context *wc)
{
	struct xnthread *curr = xnpod_current_thread();

	curr->wcontext = wc;
	wc->oldstate = xnthread_test_state(curr, XNDEFCAN);
	xnthread_set_state(curr, XNDEFCAN);
}
EXPORT_SYMBOL_GPL(xnthread_prepare_wait);

/* NOTE: caller must provide locking */
void xnthread_finish_wait(struct xnthread_wait_context *wc,
			  void (*cleanup)(struct xnthread_wait_context *wc))
{
	struct xnthread *curr = xnpod_current_thread();

	curr->wcontext = NULL;
	if ((wc->oldstate & XNDEFCAN) == 0)
		xnthread_clear_state(curr, XNDEFCAN);

	if (xnthread_test_state(curr, XNCANPND)) {
		if (cleanup)
			cleanup(wc);
		xnpod_delete_self();
	}
}
EXPORT_SYMBOL_GPL(xnthread_finish_wait);

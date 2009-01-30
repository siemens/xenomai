/*!\file sched-sporadic.c
 * \author Philippe Gerum
 * \brief POSIX SCHED_SPORADIC scheduling class
 *
 * Copyright (C) 2009 Philippe Gerum <rpm@xenomai.org>.
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

#include <nucleus/pod.h>

#define MAX_REPLENISH CONFIG_XENO_OPT_SCHED_SPORADIC_MAXREPL

static void sporadic_post_recharge(struct xnthread *thread, xnticks_t budget);

#if XENO_DEBUG(NUCLEUS)

static inline void sporadic_note_late_drop(struct xnsched *sched)
{
	/*
	 * This code should pull the break when a misconfigured
	 * sporadic thread is late on its drop date for more than a
	 * hundred times in a row. This normally reveals a time budget
	 * which is too tight.
	 */
	XENO_BUGON(NUCLEUS, ++sched->pss.drop_retries > 100);
}

static inline void sporadic_note_valid_drop(struct xnsched *sched)
{
	sched->pss.drop_retries = 0;
}

#else /* !XENO_BUGON(NUCLEUS) */

static inline void sporadic_note_late_drop(struct xnsched *sched)
{
}

static inline void sporadic_note_valid_drop(struct xnsched *sched)
{
}

#endif /* !XENO_BUGON(NUCLEUS) */

static inline xnticks_t sporadic_diff_time(xnticks_t start, xnticks_t end)
{
	xnsticks_t d = (xnsticks_t)(end - start);
	return unlikely(d < 0) ? -d : d;
}

static void sporadic_drop_handler(struct xntimer *timer)
{
	struct xnsched_sporadic_data *pss;
	union xnsched_policy_param p;
	struct xnthread *thread;

	/*
	 * XXX: this code will work properly regardless of
	 * primary/secondary mode issues.
	 */
	pss = container_of(timer, struct xnsched_sporadic_data, drop_timer);
	thread = pss->thread;

	sporadic_post_recharge(thread, pss->budget);

	if (pss->budget == 0 && thread->cprio > pss->param.low_prio) {
		if (pss->param.low_prio < 0)
			/*
			 * Special case: low_prio == -1, we want the
			 * thread to suspend until a replenishment
			 * happens.
			 */
			xnpod_suspend_thread(thread, XNHELD,
					     XN_INFINITE, XN_RELATIVE, NULL);
		else {
			p.pss.init_budget = 0;
			p.pss.current_prio = pss->param.low_prio;
			/* Move sporadic thread to the background. */
			xnpod_set_thread_schedparam(thread, &xnsched_class_sporadic, &p);
		}
	}
}

static void sporadic_schedule_drop(struct xnthread *thread)
{
	struct xnsched_sporadic_data *pss = thread->pss;
	xnticks_t now = xnpod_get_cpu_time();
	int ret;

	pss->resume_date = now;
	ret = xntimer_start(&pss->drop_timer, now + pss->budget,
			    XN_INFINITE, XN_ABSOLUTE);
	if (ret == -ETIMEDOUT) {
		sporadic_note_late_drop(thread->sched);
		sporadic_drop_handler(&pss->drop_timer);
	} else
		sporadic_note_valid_drop(thread->sched);
}

static void sporadic_replenish_handler(struct xntimer *timer)
{
	struct xnsched_sporadic_data *pss;
	union xnsched_policy_param p;
	struct xnthread *thread;
	xnticks_t now;
	int r, ret;

	pss = container_of(timer, struct xnsched_sporadic_data, repl_timer);
	thread = pss->thread;
	XENO_BUGON(NUCLEUS, pss->repl_pending <= 0);

retry:
	now = xnpod_get_cpu_time();

	do {
		r = pss->repl_out;
		if ((xnsticks_t)(now - pss->repl_data[r].date) <= 0)
			break;
		pss->budget += pss->repl_data[r].amount;
		if (pss->budget > pss->param.init_budget)
			pss->budget = pss->param.init_budget;
		pss->repl_out = (r + 1) % MAX_REPLENISH;
	} while(--pss->repl_pending > 0);

	if (pss->repl_pending > 0) {
		ret = xntimer_start(&pss->repl_timer, pss->repl_data[r].date,
				    XN_INFINITE, XN_ABSOLUTE);
		if (ret == -ETIMEDOUT)
			goto retry; /* This plugs a tiny race. */
	}

	if (pss->budget == 0)
		return;
	/*
	 * XXX: if moving to foreground priority downgrades an
	 * undergoing PIP boost, too bad, but the design flaw is in
	 * the application which should not make a sporadic thread
	 * compete for resources with higher priority classes in the
	 * first place.
	 */
	if (xnthread_test_state(thread, XNHELD))
		xnpod_resume_thread(thread, XNHELD);
	else if (thread->cprio < pss->param.normal_prio) {
		p.pss.init_budget = 0;
		p.pss.current_prio = pss->param.normal_prio;
		/* Move sporadic thread to the foreground. */
		xnpod_set_thread_schedparam(thread, &xnsched_class_sporadic, &p);
	}

	/*
	 * XXX: we have to reset the drop timer in case we preempted
	 * the thread which just got a budget increase.
	 */
	if (thread->sched->curr == thread)
		sporadic_schedule_drop(thread);
}

static void sporadic_post_recharge(struct xnthread *thread, xnticks_t budget)
{
	struct xnsched_sporadic_data *pss = thread->pss;
	int r, ret;

	if (pss->repl_pending >= pss->param.max_repl)
		return;

	if (budget > pss->budget) {
		budget = pss->budget;
		pss->budget = 0;
	} else
		pss->budget -= budget;

	r = pss->repl_in;
	pss->repl_data[r].date = pss->resume_date + pss->param.repl_period;
	pss->repl_data[r].amount = budget;
	pss->repl_in = (r + 1) % MAX_REPLENISH;
	
	if (pss->repl_pending++ == 0) {
		ret = xntimer_start(&pss->repl_timer, pss->repl_data[r].date,
				    XN_INFINITE, XN_ABSOLUTE);
		/*
		 * The following case should not happen unless the
		 * initial budget value is inappropriate, but let's
		 * handle it anyway.
		 */
		if (ret == -ETIMEDOUT)
			sporadic_replenish_handler(&pss->repl_timer);
	}
}

static void sporadic_suspend_activity(struct xnthread *thread)
{
	struct xnsched_sporadic_data *pss = thread->pss;
	xnticks_t budget;

	if (pss->budget > 0) {
		xntimer_stop(&pss->drop_timer);
		budget = sporadic_diff_time(xnpod_get_cpu_time(), pss->resume_date);
		sporadic_post_recharge(thread, budget);
	}
}

static inline void sporadic_resume_activity(struct xnthread *thread)
{
	if (thread->pss->budget > 0)
		sporadic_schedule_drop(thread);
}

static void xnsched_sporadic_init(struct xnsched *sched)
{
	/*
	 * We litterally stack the sporadic scheduler on top of the RT
	 * one, reusing its runnable and RPI queues directly. This
	 * way, RT and sporadic threads are merged into the same
	 * runqueue and thus share the same priority scale, with the
	 * addition of budget management for the sporadic ones.
	 */
#if XENO_DEBUG(NUCLEUS)
	sched->pss.drop_retries = 0;
#endif
}

static void xnsched_sporadic_setparam(struct xnthread *thread,
				      const union xnsched_policy_param *p)
{
	struct xnsched_sporadic_data *pss = thread->pss;
	/*
	 * We use the budget information to determine whether we got
	 * here from one of our internal calls to
	 * xnpod_set_thread_schedparam(), in which case we don't want
	 * to update the sporadic scheduling parameters, but only set
	 * the dynamic priority of the thread.
	 */
	if (p->pss.init_budget > 0) {
		pss->param = p->pss;
		pss->budget = p->pss.init_budget;
		pss->repl_in = 0;
		pss->repl_out = 0;
		pss->repl_pending = 0;
		if (thread == thread->sched->curr) {
			xntimer_stop(&pss->drop_timer);
			sporadic_schedule_drop(thread);
		}
	}

	thread->cprio = p->pss.current_prio;
}

static void xnsched_sporadic_getparam(struct xnthread *thread,
				      union xnsched_policy_param *p)
{
	p->pss = thread->pss->param;
	p->pss.current_prio = thread->cprio;
}

static void xnsched_sporadic_trackprio(struct xnthread *thread,
				       const union xnsched_policy_param *p)
{
	if (p)
		thread->cprio = p->pss.current_prio;
	else
		thread->cprio = thread->bprio;
}

static int xnsched_sporadic_declare(struct xnthread *thread,
				    const union xnsched_policy_param *p)
{
	struct xnsched_sporadic_data *pss;
	struct xntbase *tbase;

	if (p->pss.low_prio < -1 ||
	    p->pss.low_prio > XNSCHED_RT_MAX_PRIO)
		return -EINVAL;

	if (p->pss.normal_prio < XNSCHED_RT_MIN_PRIO ||
	    p->pss.normal_prio > XNSCHED_RT_MAX_PRIO)
		return -EINVAL;

	if (p->pss.init_budget == 0)
		return -EINVAL;

	if (p->pss.current_prio != p->pss.normal_prio)
		return -EINVAL;

	if (p->pss.repl_period < p->pss.init_budget)
		return -EINVAL;

	if (p->pss.normal_prio <= p->pss.low_prio)
		return -EINVAL;

	if (p->pss.max_repl < 1 || p->pss.max_repl > MAX_REPLENISH)
		return -EINVAL;

	pss = xnmalloc(sizeof(struct xnsched_sporadic_data));
	if (pss == NULL)
		return -ENOMEM;

	tbase = xnthread_time_base(thread);
	xntimer_init(&pss->repl_timer, tbase, sporadic_replenish_handler);
	xntimer_set_name(&pss->repl_timer, "pss-replenish");
	xntimer_init(&pss->drop_timer, tbase, sporadic_drop_handler);
	xntimer_set_name(&pss->drop_timer, "pss-drop");

	thread->pss = pss;
	pss->thread = thread;

	return 0;
}

static void xnsched_sporadic_forget(struct xnthread *thread)
{
	struct xnsched_sporadic_data *pss = thread->pss;

	xntimer_destroy(&pss->repl_timer);
	xntimer_destroy(&pss->drop_timer);
	xnfree(pss);
	thread->pss = NULL;
}

static void xnsched_sporadic_enqueue(struct xnthread *thread)
{
	__xnsched_rt_enqueue(thread);
}

static void xnsched_sporadic_dequeue(struct xnthread *thread)
{
	__xnsched_rt_dequeue(thread);
}

static void xnsched_sporadic_requeue(struct xnthread *thread)
{
	__xnsched_rt_requeue(thread);
}

static struct xnthread *xnsched_sporadic_pick(struct xnsched *sched)
{
	struct xnthread *curr = sched->curr, *next;
	struct xnpholder *h;

	h = sched_getpq(&sched->rt.runnable);
	next = h ? link2thread(h, rlink) : NULL;

	if (curr == next)
		return next;

	/* Arm the drop timer for an incoming sporadic thread. */
	if (next && next->pss)
		sporadic_resume_activity(next);

	/*
	 * Do not consider an outgoing thread that temporarily moved
	 * to the sporadic scheduling class (i.e. PIP enforcement): it
	 * has an infinite time budget to release asap what some
	 * sporadic thread wants, so there is no replenishment
	 * operation involved.
	 */
	if (curr->base_class != &xnsched_class_sporadic)
		return next;

	/*
	 * We are about to block or preempt a sporadic thread. Clear
	 * the drop timer, then schedule a replenishment operation.
	 */
	sporadic_suspend_activity(curr);

	return next;
}

#ifdef CONFIG_XENO_OPT_PRIOCPL

static struct xnthread *xnsched_sporadic_push_rpi(struct xnsched *sched,
						  struct xnthread *thread)
{
	return __xnsched_rt_push_rpi(sched, thread);
}

static void xnsched_sporadic_pop_rpi(struct xnthread *thread)
{
	__xnsched_rt_pop_rpi(thread);
}

static struct xnthread *xnsched_sporadic_peek_rpi(struct xnsched *sched)
{
	return __xnsched_rt_peek_rpi(sched);
}

static void xnsched_sporadic_suspend_rpi(struct xnthread *thread)
{
	if (thread->pss)
		sporadic_suspend_activity(thread);
}

static void xnsched_sporadic_resume_rpi(struct xnthread *thread)
{
	if (thread->pss)
		sporadic_resume_activity(thread);
}

#endif /* CONFIG_XENO_OPT_PRIOCPL */

struct xnsched_class xnsched_class_sporadic = {

	.sched_init		=	xnsched_sporadic_init,
	.sched_enqueue		=	xnsched_sporadic_enqueue,
	.sched_dequeue		=	xnsched_sporadic_dequeue,
	.sched_requeue		=	xnsched_sporadic_requeue,
	.sched_pick		=	xnsched_sporadic_pick,
	.sched_tick		=	NULL,
	.sched_rotate		=	NULL,
	.sched_migrate		=	NULL,
	.sched_setparam		=	xnsched_sporadic_setparam,
	.sched_getparam		=	xnsched_sporadic_getparam,
	.sched_trackprio	=	xnsched_sporadic_trackprio,
	.sched_declare		=	xnsched_sporadic_declare,
	.sched_forget		=	xnsched_sporadic_forget,
#ifdef CONFIG_XENO_OPT_PRIOCPL
	.sched_push_rpi 	=	xnsched_sporadic_push_rpi,
	.sched_pop_rpi		=	xnsched_sporadic_pop_rpi,
	.sched_peek_rpi 	=	xnsched_sporadic_peek_rpi,
	.sched_suspend_rpi 	=	xnsched_sporadic_suspend_rpi,
	.sched_resume_rpi 	=	xnsched_sporadic_resume_rpi,
#endif
	.weight			=	XNSCHED_CLASS_WEIGHT(1),
	.name			=	"pss"
};

EXPORT_SYMBOL_GPL(xnsched_class_sporadic);

static int __init register_sched_class(void)
{
	return xnsched_register_class(&xnsched_class_sporadic);
}

__initcall(register_sched_class);

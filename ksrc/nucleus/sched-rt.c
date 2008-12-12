/*!\file sched-rt.c
 * \author Philippe Gerum
 * \brief Common real-time scheduling class implementation (FIFO + RR)
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

#include <nucleus/pod.h>

static void xnsched_rt_init(struct xnsched *sched)
{
	sched_initpq(&sched->rt.runnable, XNSCHED_RT_MIN_PRIO, XNSCHED_RT_MAX_PRIO);
#ifdef CONFIG_XENO_OPT_PRIOCPL
	sched_initpq(&sched->rt.relaxed, XNSCHED_RT_MIN_PRIO, XNSCHED_RT_MAX_PRIO);
#endif
}

static void xnsched_rt_requeue(struct xnthread *thread)
{
	/*
	 * Put back at same place: i.e. requeue to head of current
	 * priority group (i.e. LIFO, used for preemption handling).
	 */
	__xnsched_rt_requeue(thread);
}

static void xnsched_rt_enqueue(struct xnthread *thread)
{
	/*
	 * Enqueue for next pick: i.e. move to end of current priority
	 * group (i.e. FIFO).
	 */
	__xnsched_rt_enqueue(thread);
}

static void xnsched_rt_dequeue(struct xnthread *thread)
{
	/*
	 * Pull from the runnable thread queue.
	 */
	__xnsched_rt_dequeue(thread);
}

static void xnsched_rt_rotate(struct xnsched *sched,
			      const union xnsched_policy_param *p)
{
	struct xnthread *thread, *curr;
	struct xnpholder *h;

	if (sched_emptypq_p(&sched->rt.runnable))
		return;	/* No runnable thread in this class. */

	curr = sched->curr;

	if (p->rt.prio == XNSCHED_RUNPRIO)
		thread = curr;
	else {
		h = sched_findpqh(&sched->rt.runnable, p->rt.prio);
		if (h == NULL)
			return;
		thread = link2thread(h, rlink);
	}

	/*
	 * In case we picked the current thread, we have to make sure
	 * not to move it back to the runnable queue if it was blocked
	 * before we were called. The same goes if the current thread
	 * holds the scheduler lock.
	 */
	if (thread == curr &&
	    xnthread_test_state(curr, XNTHREAD_BLOCK_BITS | XNLOCK))
		return;

	xnsched_putback(thread);
}

static struct xnthread *xnsched_rt_pick(struct xnsched *sched)
{
	return __xnsched_rt_pick(sched);
}

void xnsched_rt_tick(struct xnthread *curr)
{
	/*
	 * The round-robin time credit is only consumed by a running
	 * thread that neither holds the scheduler lock nor was
	 * blocked before entering this callback.
	 */
	if (likely(curr->rrcredit > 1))
		--curr->rrcredit;
	else {
		/*
		 * If the time slice is exhausted for the running
		 * thread, move it back to the runnable queue at the
		 * end of its priority group and reset its credit for
		 * the next run.
		 */
		curr->rrcredit = curr->rrperiod;
		xnsched_putback(curr);
	}
}

void xnsched_rt_setparam(struct xnthread *thread,
			 const union xnsched_policy_param *p)
{
	__xnsched_rt_setparam(thread, p);
}

void xnsched_rt_getparam(struct xnthread *thread,
			 union xnsched_policy_param *p)
{
	__xnsched_rt_getparam(thread, p);
}

void xnsched_rt_trackprio(struct xnthread *thread,
			  const union xnsched_policy_param *p)
{
	__xnsched_rt_trackprio(thread, p);
}

#ifdef CONFIG_XENO_OPT_PRIOCPL

static struct xnthread *xnsched_rt_push_rpi(struct xnsched *sched,
					    struct xnthread *thread)
{
	return __xnsched_rt_push_rpi(sched, thread);
}

static void xnsched_rt_pop_rpi(struct xnthread *thread)
{
	__xnsched_rt_pop_rpi(thread);
}

static struct xnthread *xnsched_rt_peek_rpi(struct xnsched *sched)
{
	return __xnsched_rt_peek_rpi(sched);
}

#endif /* CONFIG_XENO_OPT_PRIOCPL */

struct xnsched_class xnsched_class_rt = {

	.sched_init		=	xnsched_rt_init,
	.sched_enqueue		=	xnsched_rt_enqueue,
	.sched_dequeue		=	xnsched_rt_dequeue,
	.sched_requeue		=	xnsched_rt_requeue,
	.sched_pick		=	xnsched_rt_pick,
	.sched_tick		=	xnsched_rt_tick,
	.sched_rotate		=	xnsched_rt_rotate,
	.sched_setparam		=	xnsched_rt_setparam,
	.sched_trackprio	=	xnsched_rt_trackprio,
	.sched_getparam		=	xnsched_rt_getparam,
#ifdef CONFIG_XENO_OPT_PRIOCPL
	.sched_push_rpi 	=	xnsched_rt_push_rpi,
	.sched_pop_rpi		=	xnsched_rt_pop_rpi,
	.sched_peek_rpi 	=	xnsched_rt_peek_rpi,
#endif
	.next			=	&xnsched_class_idle,
	.weight			=	XNSCHED_CLASS_WEIGHT(1),
	.name			=	"rt"
};

EXPORT_SYMBOL(xnsched_class_rt);

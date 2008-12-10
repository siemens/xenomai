/*!\file sched.c
 * \author Philippe Gerum
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
#include <nucleus/thread.h>
#include <nucleus/timer.h>
#include <nucleus/assert.h>
#include <asm/xenomai/bits/sched.h>

#ifndef CONFIG_XENO_OPT_DEBUG_NUCLEUS
#define CONFIG_XENO_OPT_DEBUG_NUCLEUS 0
#endif

#define xnsched_class_highest	(&xnsched_class_rt)

#define for_each_xnsched_class(p) \
   for (p = xnsched_class_highest; p; p = p->next)

#ifdef CONFIG_XENO_OPT_WATCHDOG

/*! 
 * @internal
 * \fn void xnsched_watchdog_handler(struct xntimer *timer)
 * \brief Process watchdog ticks.
 *
 * This internal routine handles incoming watchdog ticks to detect
 * software lockups. It kills any offending thread which is found to
 * monopolize the CPU so as to starve the Linux kernel for more than
 * four seconds.
 */

static void xnsched_watchdog_handler(struct xntimer *timer)
{
	struct xnsched *sched = xnpod_current_sched();
	struct xnthread *thread = sched->curr;

	if (likely(xnthread_test_state(thread, XNROOT))) {
		xnsched_reset_watchdog(sched);
		return;
	}
		
	if (unlikely(++sched->wdcount >= CONFIG_XENO_OPT_WATCHDOG_TIMEOUT)) {
		trace_mark(xn_nucleus_watchdog, "thread %p thread_name %s",
			   thread, xnthread_name(thread));
		xnprintf("watchdog triggered -- killing runaway thread '%s'\n",
			 xnthread_name(thread));
		xnpod_delete_thread(thread);
		xnsched_reset_watchdog(sched);
	}
}

#endif /* CONFIG_XENO_OPT_WATCHDOG */

void xnsched_init(struct xnsched *sched)
{
	char htimer_name[XNOBJECT_NAME_LEN];
	char root_name[XNOBJECT_NAME_LEN];
	int cpu = xnsched_cpu(sched);
	struct xnsched_class *p;

	for_each_xnsched_class(p) {
		XENO_BUGON(NUCLEUS,
			   p->weight > INT_MAX / XNSCHED_CLASS_MAX_THREADS);
		if (p->sched_init)
			p->sched_init(sched);
	}

#ifdef CONFIG_SMP
	sprintf(htimer_name, "[host-timer/%u]", cpu);
	sprintf(root_name, "ROOT/%u", cpu);
#else
	strcpy(htimer_name, "[host-timer]");
	strcpy(root_name, "ROOT");
#endif
	sched->status = 0;
	sched->inesting = 0;
	sched->curr = &sched->rootcb;
#ifdef CONFIG_XENO_OPT_PRIOCPL
	xnlock_init(&sched->rpilock);
#endif
	/*
	 * No direct handler here since the host timer processing is
	 * postponed to xnintr_irq_handler(), as part of the interrupt
	 * exit code.
	 */
	xntimer_init(&sched->htimer, &nktbase, NULL);
	xntimer_set_priority(&sched->htimer, XNTIMER_LOPRIO);
	xntimer_set_name(&sched->htimer, htimer_name);
	xntimer_set_sched(&sched->htimer, sched);
	sched->zombie = NULL;
	xnarch_cpus_clear(sched->resched);

	xnthread_init(&sched->rootcb,
		      &nktbase,
		      root_name, XNSCHED_IDLE_PRIO,
		      XNROOT | XNSTARTED | XNFPU,
		      XNARCH_ROOT_STACKSZ,
		      NULL);
	sched->rootcb.sched = sched;
	sched->rootcb.affinity = xnarch_cpumask_of_cpu(cpu);
	xnstat_exectime_set_current(sched, &sched->rootcb.stat.account);
#ifdef CONFIG_XENO_HW_FPU
	sched->fpuholder = &sched->rootcb;
#endif /* CONFIG_XENO_HW_FPU */

	xnarch_init_root_tcb(xnthread_archtcb(&sched->rootcb),
			     &sched->rootcb,
			     xnthread_name(&sched->rootcb));

#ifdef CONFIG_XENO_OPT_WATCHDOG
	xntimer_init(&sched->wdtimer, &nktbase, xnsched_watchdog_handler);
	xntimer_set_name(&sched->wdtimer, "[watchdog]");
	xntimer_set_priority(&sched->wdtimer, XNTIMER_LOPRIO);
	xntimer_set_sched(&sched->wdtimer, sched);
#endif /* CONFIG_XENO_OPT_WATCHDOG */
	xntimerq_init(&sched->timerqueue);
}

void xnsched_destroy(struct xnsched *sched)
{
	xntimer_destroy(&sched->htimer);
	xntimer_destroy(&sched->rootcb.ptimer);
	xntimer_destroy(&sched->rootcb.rtimer);
#ifdef CONFIG_XENO_OPT_WATCHDOG
	xntimer_destroy(&sched->wdtimer);
#endif /* CONFIG_XENO_OPT_WATCHDOG */
	xntimerq_destroy(&sched->timerqueue);
}

/* Must be called with nklock locked, interrupts off. */
struct xnthread *xnsched_pick_next(struct xnsched *sched)
{
	struct xnthread *curr = sched->curr;
	struct xnsched_class *p;
	struct xnthread *thread;

	if (!xnthread_test_state(curr, XNTHREAD_BLOCK_BITS | XNZOMBIE)) {
		/*
		 * Do not preempt the current thread if it holds the
		 * scheduler lock.
		 */
		if (xnthread_test_state(curr, XNLOCK)) {
			xnsched_set_self_resched(sched);
			return curr;
		}
		/*
		 * Push the current thread back to the runnable queue
		 * of the scheduling class it belongs to, if not yet
		 * linked to it (XNREADY tells us if it is).
		 */
		if (!xnthread_test_state(curr, XNREADY)) {
			xnsched_requeue(curr);
			xnthread_set_state(curr, XNREADY);
		}
#ifdef __XENO_SIM__
		if (nkpod->schedhook)
			nkpod->schedhook(curr, XNREADY);
#endif /* __XENO_SIM__ */
	}

	/*
	 * Find the runnable thread having the highest priority among
	 * all scheduling classes, scanned by decreasing priority.
	 */
#ifdef CONFIG_XENO_OPT_SCHED_CLASSES
	for_each_xnsched_class(p) {
		thread = p->sched_pick(sched);
		if (thread) {
			xnthread_clear_state(thread, XNREADY);
			return thread;
		}
	}

	return NULL; /* Never executed because of the idle class. */
#else /* !CONFIG_XENO_OPT_SCHED_CLASSES */
	thread = __xnsched_rt_pick(sched); (void)p;
	if (likely(thread)) {
		xnthread_clear_state(thread, XNREADY);
		return thread;
	}

	return &sched->rootcb;
#endif /* CONFIG_XENO_OPT_SCHED_CLASSES */
}

/* Must be called with nklock locked, interrupts off. */
void xnsched_zombie_hooks(struct xnthread *thread)
{
	XENO_BUGON(NUCLEUS, thread->sched->zombie != NULL);
	thread->sched->zombie = thread;

	trace_mark(xn_nucleus_sched_finalize,
		   "thread_out %p thread_out_name %s",
		   thread, xnthread_name(thread));

	if (!emptyq_p(&nkpod->tdeleteq)
	    && !xnthread_test_state(thread, XNROOT)) {
		trace_mark(xn_nucleus_thread_callout,
			   "thread %p thread_name %s hook %s",
			   thread, xnthread_name(thread), "DELETE");
		xnpod_fire_callouts(&nkpod->tdeleteq, thread);
	}

	xnsched_forget(thread);
}

#ifdef CONFIG_XENO_OPT_PRIOCPL

/* Must be called with nklock locked, interrupts off. */
struct xnthread *xnsched_peek_rpi(struct xnsched *sched)
{
	struct xnsched_class *p;
	struct xnthread *thread;

	/*
	 * Find the relaxed thread having the highest priority among
	 * all scheduling classes, scanned by decreasing priority.
	 */
#ifdef CONFIG_XENO_OPT_SCHED_CLASSES
	for_each_xnsched_class(p) {
		if (p->sched_peek_rpi) {
			thread = p->sched_peek_rpi(sched);
			if (thread)
				return thread;
		}
	}

	return NULL;
#else /* !CONFIG_XENO_OPT_SCHED_CLASSES */
	thread = __xnsched_rt_peek_rpi(sched); (void)p;
	return thread;
#endif /* CONFIG_XENO_OPT_SCHED_CLASSES */
}

/*! 
 * @internal
 * \fn void xnsched_renice_root(struct xnsched *sched, struct xnthread *target)
 * \brief Change the root thread priority.
 *
 * xnsched_renice_root() updates the current priority of the root
 * thread for the given scheduler slot. This may lead to changing the
 * scheduling class of the root thread.
 */
void xnsched_renice_root(struct xnsched *sched, struct xnthread *target)
{
	struct xnthread *root = &sched->rootcb;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (target == NULL)
		target = root;

	xnsched_track_policy(root, target);

	trace_mark(xn_nucleus_sched_reniceroot, MARK_NOARGS);
	xnarch_trace_pid(xnarch_user_pid(xnthread_archtcb(root)), root->cprio);

	xnlock_put_irqrestore(&nklock, s);
}

#endif /* CONFIG_XENO_OPT_PRIOCPL */

#ifdef CONFIG_XENO_HW_UNLOCKED_SWITCH

struct xnsched *xnsched_finish_unlocked_switch(struct xnsched *sched)
{
	struct xnthread *last;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

#ifdef CONFIG_SMP
	/* If current thread migrated while suspended */
	sched = xnpod_current_sched();
#endif /* CONFIG_SMP */

	last = sched->last;
	__clrbits(sched->status, XNSWLOCK);

	/* Detect a thread which called xnpod_migrate_thread */
	if (last->sched != sched) {
		xnsched_putback(last);
		xnthread_clear_state(last, XNMIGRATE);
	}

	if (xnthread_test_state(last, XNZOMBIE)) {
		/*
		 * There are two cases where sched->last has the zombie
		 * bit:
		 * - either it had it before the context switch, the hooks
		 * have been executed and sched->zombie is last;
		 * - or it has been killed while the nklocked was unlocked
		 * during the context switch, in which case we must run the
		 * hooks, and we do it now.
		 */
		if (sched->zombie != last)
			xnsched_zombie_hooks(last);
	}

	return sched;
}

void xnsched_resched_after_unlocked_switch(void)
{
	if (xnsched_resched_p(xnpod_current_sched()))
		xnpod_schedule();
}

#endif /* CONFIG_XENO_HW_UNLOCKED_SWITCH */

/* Must be called with nklock locked, interrupts off. */
void xnsched_putback(struct xnthread *thread)
{
	if (xnthread_test_state(thread, XNREADY))
		xnsched_dequeue(thread);
	else
		xnthread_set_state(thread, XNREADY);

	xnsched_enqueue(thread);
	xnsched_set_resched(thread->sched);
}

/* Must be called with nklock locked, interrupts off. */
void xnsched_set_policy(struct xnthread *thread,
			struct xnsched_class *sched_class,
			const union xnsched_policy_param *p)
{
	if (xnthread_test_state(thread, XNREADY))
		xnsched_dequeue(thread);

	if (sched_class != thread->base_class)
		xnsched_forget(thread);

	thread->sched_class = sched_class;
	thread->base_class = sched_class;
	xnsched_setparam(thread, p);

	if (xnthread_test_state(thread, XNREADY))
		xnsched_enqueue(thread);

	xnsched_set_resched(thread->sched);
}

/* Must be called with nklock locked, interrupts off. */
void xnsched_track_policy(struct xnthread *thread,
			  struct xnthread *target)
{
	union xnsched_policy_param param;

	if (xnthread_test_state(thread, XNREADY))
		xnsched_dequeue(thread);
	/*
	 * Self-targeting means to reset the scheduling policy and
	 * parameters to the base ones. Otherwise, make thread inherit
	 * the scheduling data from target.
	 */
	if (target == thread) {
		thread->sched_class = thread->base_class;
		xnsched_trackprio(thread, NULL);
	} else {
		xnsched_getparam(target, &param);
		thread->sched_class = target->sched_class;
		xnsched_trackprio(thread, &param);
	}

	if (xnthread_test_state(thread, XNREADY))
		xnsched_enqueue(thread);

	xnsched_set_resched(thread->sched);
}

/* Must be called with nklock locked, interrupts off. thread must be
 * runnable. */
void xnsched_migrate(struct xnthread *thread, struct xnsched *sched)
{
	struct xnsched_class *sched_class = thread->sched_class;

	if (xnthread_test_state(thread, XNREADY)) {
		xnsched_dequeue(thread);
		xnthread_clear_state(thread, XNREADY);
	}

	if (sched_class->sched_migrate)
		sched_class->sched_migrate(thread, sched);
	/*
	 * WARNING: the scheduling class may have just changed as a
	 * result of calling the per-class migration hook.
	 */
	xnsched_set_resched(thread->sched);
	thread->sched = sched;

#ifdef CONFIG_XENO_HW_UNLOCKED_SWITCH
	/*
	 * Mark the thread in flight, xnsched_finish_unlocked_switch()
	 * will put the thread on the remote runqueue.
	 */
	xnthread_set_state(thread, XNMIGRATE);
#else /* !CONFIG_XENO_HW_UNLOCKED_SWITCH */
	/* Move thread to the remote runnable queue. */
	xnsched_putback(thread);
#endif /* !CONFIG_XENO_HW_UNLOCKED_SWITCH */
}

/* Must be called with nklock locked, interrupts off. thread may be
 * blocked. */
void xnsched_migrate_passive(struct xnthread *thread, struct xnsched *sched)
{
	struct xnsched_class *sched_class = thread->sched_class;

	if (xnthread_test_state(thread, XNREADY)) {
		xnsched_dequeue(thread);
		xnthread_clear_state(thread, XNREADY);
	}

	if (sched_class->sched_migrate)
		sched_class->sched_migrate(thread, sched);
	/*
	 * WARNING: the scheduling class may have just changed as a
	 * result of calling the per-class migration hook.
	 */
	xnsched_set_resched(thread->sched);
	thread->sched = sched;

	if (!xnthread_test_state(thread, XNTHREAD_BLOCK_BITS)) {
		xnsched_requeue(thread);
		xnthread_set_state(thread, XNREADY);
	}
}

/*!
 * \fn void xnsched_rotate(int prio)
 * \brief Rotate a priority level in the runqueue.
 *
 * The active scheduling class is requested to rotate its runqueue for
 * the current CPU. Rotation is performed on the priority level
 * specified by @prio. A scheduling class is active when the current
 * thread belongs to it.
 *
 * For instance, a round-robin scheduling policy may be implemented by
 * periodically issuing this call when the RT class is active, in
 * which case the thread leading the specified priority group will be
 * moved at the end of the latter.
 *
 * @note The nucleus already provides a built-in round-robin mode for
 * the RT class (see xnpod_activate_rr()).
 *
 * @param prio The priority level to rotate. if XNSCHED_RUNPRIO is
 * given, the priority of the currently running thread is used to
 * rotate the queue.
 *
 * Environments:
 *
 * This service should be called from:
 *
 * - Kernel-based task
 * - Interrupt service routine (preempting a Xenomai thread)
 * - User-space task (primary mode only)
 *
 * Rescheduling: never.
 */

void xnsched_rotate(int prio)
{
	struct xnsched_class *sched_class;
	struct xnsched *sched;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	sched = xnpod_current_sched();
	sched_class = sched->curr->sched_class;

	if (sched_class->sched_rotate)
		sched_class->sched_rotate(sched, prio);

	trace_mark(xn_nucleus_sched_rotate, "priority %d", prio);

	xnlock_put_irqrestore(&nklock, s);
}

#ifdef CONFIG_XENO_OPT_SCALABLE_SCHED

#ifndef CONFIG_XENO_OPT_DEBUG_QUEUES
#define CONFIG_XENO_OPT_DEBUG_QUEUES 0
#endif

void initmlq(struct xnsched_mlq *q, int loprio, int hiprio)
{
	int prio;

	q->elems = 0;
	q->loprio = loprio;
	q->hiprio = hiprio;
	q->himap = 0;
	memset(&q->lomap, 0, sizeof(q->lomap));

	for (prio = 0; prio < XNSCHED_MLQ_LEVELS; prio++)
		initq(&q->queue[prio]);

	XENO_ASSERT(QUEUES, 
		    hiprio - loprio + 1 < XNSCHED_MLQ_LEVELS,
		    xnpod_fatal("priority range [%d..%d] is beyond multi-level "
				"queue indexing capabilities",
				loprio, hiprio));
}

void addmlq(struct xnsched_mlq *q,
	    struct xnpholder *h, int idx, int lifo)
{
	struct xnqueue *queue = &q->queue[idx];
	int hi = idx / BITS_PER_LONG;
	int lo = idx % BITS_PER_LONG;

	if (lifo)
		prependq(queue, &h->plink);
	else
		appendq(queue, &h->plink);

	h->prio = idx;
	q->elems++;
	__setbits(q->himap, 1UL << hi);
	__setbits(q->lomap[hi], 1UL << lo);
}

void removemlq(struct xnsched_mlq *q, struct xnpholder *h)
{
	int idx = h->prio;
	struct xnqueue *queue = &q->queue[idx];

	q->elems--;

	removeq(queue, &h->plink);

	if (emptyq_p(queue)) {
		int hi = idx / BITS_PER_LONG;
		int lo = idx % BITS_PER_LONG;
		__clrbits(q->lomap[hi], 1UL << lo);
		if (q->lomap[hi] == 0)
			__clrbits(q->himap, 1UL << hi);
	}
}

struct xnpholder *findmlqh(struct xnsched_mlq *q, int prio)
{
	struct xnqueue *queue = &q->queue[indexmlq(q, prio)];
	return (struct xnpholder *)getheadq(queue);
}

struct xnpholder *getheadmlq(struct xnsched_mlq *q)
{
	struct xnqueue *queue;
	struct xnpholder *h;

	if (emptymlq_p(q))
		return NULL;

	queue = &q->queue[ffsmlq(q)];
	h = (struct xnpholder *)getheadq(queue);

	XENO_ASSERT(QUEUES, h,
		    xnpod_fatal
		    ("corrupted multi-level queue, qslot=%p at %s:%d", q,
		     __FILE__, __LINE__);
		);

	return h;
}

struct xnpholder *getmlq(struct xnsched_mlq *q)
{
	struct xnqueue *queue;
	struct xnholder *h;
	int idx, hi, lo;

	if (emptymlq_p(q))
		return NULL;

	idx = ffsmlq(q);
	queue = &q->queue[idx];
	h = getq(queue);

	XENO_ASSERT(QUEUES, h,
		    xnpod_fatal
		    ("corrupted multi-level queue, qslot=%p at %s:%d", q,
		     __FILE__, __LINE__);
	    );

	q->elems--;

	if (emptyq_p(queue)) {
		hi = idx / BITS_PER_LONG;
		lo = idx % BITS_PER_LONG;
		__clrbits(q->lomap[hi], 1UL << lo);
		if (q->lomap[hi] == 0)
			__clrbits(q->himap, 1UL << hi);
	}

	return (struct xnpholder *)h;
}

struct xnpholder *nextmlq(struct xnsched_mlq *q, struct xnpholder *h)
{
	unsigned long hibits, lobits;
	int idx = h->prio, hi, lo;
	struct xnqueue *queue;
	struct xnholder *nh;

	hi = idx / BITS_PER_LONG;
	lo = idx % BITS_PER_LONG;
	lobits = q->lomap[hi] >> lo;
	hibits = q->himap >> hi;

	for (;;) {
		queue = &q->queue[idx];
		if (!emptyq_p(queue)) {
			nh = h ? nextq(queue, &h->plink) : getheadq(queue);
			if (nh)
				return (struct xnpholder *)nh;
		}
		for (;;) {
			lobits >>= 1;
			if (lobits == 0) {
				hibits >>= 1;
				if (hibits == 0)
					return NULL;
				lobits = q->lomap[++hi];
				idx = hi * BITS_PER_LONG;
			} else
				idx++;
			if (lobits & 1) {
				h = NULL;
				break;
			}
		}
	}

	return NULL;
}

#endif /* CONFIG_XENO_OPT_SCALABLE_SCHED */

EXPORT_SYMBOL(xnsched_rotate);
EXPORT_SYMBOL(xnsched_set_policy);

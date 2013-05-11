/*!\file nucleus/sched.c
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
#include <nucleus/intr.h>
#include <nucleus/heap.h>
#include <asm/xenomai/bits/sched.h>

static struct xnsched_class *xnsched_class_highest;

#define for_each_xnsched_class(p) \
   for (p = xnsched_class_highest; p; p = p->next)

static void xnsched_register_class(struct xnsched_class *sched_class)
{
	sched_class->next = xnsched_class_highest;
	xnsched_class_highest = sched_class;

	/*
	 * Classes shall be registered by increasing priority order,
	 * idle first and up.
	 */
	XENO_BUGON(NUCLEUS, sched_class->next &&
		   sched_class->next->weight > sched_class->weight);

	xnloginfo("scheduling class %s registered.\n", sched_class->name);
}

void xnsched_register_classes(void)
{
	xnsched_register_class(&xnsched_class_idle);
#ifdef CONFIG_XENO_OPT_SCHED_TP
	xnsched_register_class(&xnsched_class_tp);
#endif
#ifdef CONFIG_XENO_OPT_SCHED_SPORADIC
	xnsched_register_class(&xnsched_class_sporadic);
#endif
	xnsched_register_class(&xnsched_class_rt);
}

#ifdef CONFIG_XENO_OPT_WATCHDOG

static u_long wd_timeout_arg = CONFIG_XENO_OPT_WATCHDOG_TIMEOUT;
module_param_named(watchdog_timeout, wd_timeout_arg, ulong, 0644);
MODULE_PARM_DESC(watchdog_timeout, "Watchdog timeout (s)");

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

	if (likely(++sched->wdcount < wd_timeout_arg))
		return;

#ifdef CONFIG_XENO_OPT_PERVASIVE
	if (xnthread_test_state(thread, XNSHADOW) &&
	    !xnthread_amok_p(thread)) {
		trace_mark(xn_nucleus, watchdog_signal,
			   "thread %p thread_name %s",
			   thread, xnthread_name(thread));
		xnprintf("watchdog triggered -- signaling runaway thread "
			 "'%s'\n", xnthread_name(thread));
		xnthread_set_info(thread, XNAMOK);
		xnshadow_send_sig(thread, SIGDEBUG, SIGDEBUG_WATCHDOG, 1);
		xnshadow_call_mayday(thread);
	} else
#endif /* CONFIG_XENO_OPT_PERVASIVE */
	{
		trace_mark(xn_nucleus, watchdog, "thread %p thread_name %s",
			   thread, xnthread_name(thread));
		xnprintf("watchdog triggered -- killing runaway thread '%s'\n",
			 xnthread_name(thread));
		xnpod_delete_thread(thread);
	}
	xnsched_reset_watchdog(sched);
}

#endif /* CONFIG_XENO_OPT_WATCHDOG */

void xnsched_init(struct xnsched *sched, int cpu)
{
	char htimer_name[XNOBJECT_NAME_LEN];
	char root_name[XNOBJECT_NAME_LEN];
	union xnsched_policy_param param;
	struct xnthread_init_attr attr;
	struct xnsched_class *p;

	sched->cpu = cpu;

	for_each_xnsched_class(p) {
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
	sched->lflags = 0;
	sched->inesting = 0;
	sched->curr = &sched->rootcb;
#ifdef CONFIG_XENO_OPT_PRIOCPL
	xnlock_init(&sched->rpilock);
	sched->rpistatus = 0;
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
#ifdef CONFIG_SMP
	xnarch_cpus_clear(sched->resched);
#endif

	attr.flags = XNROOT | XNSTARTED | XNFPU;
	attr.name = root_name;
	attr.stacksize = 0;
	attr.tbase = &nktbase;
	attr.ops = NULL;
	param.idle.prio = XNSCHED_IDLE_PRIO;

	xnthread_init(&sched->rootcb, &attr,
		      sched, &xnsched_class_idle, &param);

	sched->rootcb.affinity = xnarch_cpumask_of_cpu(cpu);
	xnstat_exectime_set_current(sched, &sched->rootcb.stat.account);
#ifdef CONFIG_XENO_HW_FPU
	sched->fpuholder = &sched->rootcb;
#endif /* CONFIG_XENO_HW_FPU */

	xnarch_init_root_tcb(xnthread_archtcb(&sched->rootcb),
			     &sched->rootcb,
			     xnthread_name(&sched->rootcb));

#ifdef CONFIG_XENO_OPT_WATCHDOG
	xntimer_init_noblock(&sched->wdtimer, &nktbase,
			     xnsched_watchdog_handler);
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
	if (unlikely(thread == NULL))
		thread = &sched->rootcb;

	xnthread_clear_state(thread, XNREADY);

	return thread;
#endif /* CONFIG_XENO_OPT_SCHED_CLASSES */
}

/* Must be called with nklock locked, interrupts off. */
void xnsched_zombie_hooks(struct xnthread *thread)
{
	XENO_BUGON(NUCLEUS, thread->sched->zombie != NULL);
	thread->sched->zombie = thread;

	trace_mark(xn_nucleus, sched_finalize,
		   "thread_out %p thread_out_name %s",
		   thread, xnthread_name(thread));

	xnpod_run_hooks(&nkpod->tdeleteq, thread, "DELETE");

	xnsched_forget(thread);
}

void __xnsched_finalize_zombie(struct xnsched *sched)
{
	struct xnthread *thread = sched->zombie;

	xnthread_cleanup_tcb(thread);

	xnarch_finalize_no_switch(xnthread_archtcb(thread));

	if (xnthread_test_state(sched->curr, XNROOT))
		xnfreesync();

	sched->zombie = NULL;
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

	trace_mark(xn_nucleus, sched_reniceroot, MARK_NOARGS);
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
	(void)s;

#ifdef CONFIG_SMP
	/* If current thread migrated while suspended */
	sched = xnpod_current_sched();
#endif /* CONFIG_SMP */

	last = sched->last;
	__clrbits(sched->status, XNINSW);

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
int xnsched_set_policy(struct xnthread *thread,
		       struct xnsched_class *sched_class,
		       const union xnsched_policy_param *p)
{
	int ret;

	/*
	 * Declaring a thread to a new scheduling class may fail, so
	 * we do that early, while the thread is still a member of the
	 * previous class. However, this also means that the
	 * declaration callback shall not do anything that might
	 * affect the previous class (such as touching thread->rlink
	 * for instance).
	 */
	if (sched_class != thread->base_class) {
		if (sched_class->sched_declare) {
			ret = sched_class->sched_declare(thread, p);
			if (ret)
				return ret;
		}
		sched_class->nthreads++;
	}

	/*
	 * As a special case, we may be called from xnthread_init()
	 * with no previous scheduling class at all.
	 */
	if (likely(thread->base_class != NULL)) {
		if (xnthread_test_state(thread, XNREADY))
			xnsched_dequeue(thread);

		if (sched_class != thread->base_class)
			xnsched_forget(thread);
	}

	thread->sched_class = sched_class;
	thread->base_class = sched_class;
	xnsched_setparam(thread, p);
	thread->bprio = thread->cprio;

	if (xnthread_test_state(thread, XNREADY))
		xnsched_enqueue(thread);

	if (xnthread_test_state(thread, XNSTARTED))
		xnsched_set_resched(thread->sched);

	return 0;
}
EXPORT_SYMBOL_GPL(xnsched_set_policy);

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

#ifdef CONFIG_XENO_OPT_SCALABLE_SCHED

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

#ifdef CONFIG_XENO_OPT_VFILE

static struct xnvfile_directory schedclass_vfroot;

struct vfile_sched_priv {
	struct xnholder *curr;
	xnticks_t start_time;
};

struct vfile_sched_data {
	int cpu;
	pid_t pid;
	char name[XNOBJECT_NAME_LEN];
	char timebase[XNOBJECT_NAME_LEN];
	char sched_class[XNOBJECT_NAME_LEN];
	int cprio;
	int dnprio;
	int periodic;
	xnticks_t timeout;
	xnflags_t state;
};

static struct xnvfile_snapshot_ops vfile_sched_ops;

static struct xnvfile_snapshot sched_vfile = {
	.privsz = sizeof(struct vfile_sched_priv),
	.datasz = sizeof(struct vfile_sched_data),
	.tag = &nkpod_struct.threadlist_tag,
	.ops = &vfile_sched_ops,
};

static int vfile_sched_rewind(struct xnvfile_snapshot_iterator *it)
{
	struct vfile_sched_priv *priv = xnvfile_iterator_priv(it);

	priv->curr = getheadq(&nkpod->threadq);
	priv->start_time = xntbase_get_jiffies(&nktbase);

	return countq(&nkpod->threadq);
}

static int vfile_sched_next(struct xnvfile_snapshot_iterator *it, void *data)
{
	struct vfile_sched_priv *priv = xnvfile_iterator_priv(it);
	struct vfile_sched_data *p = data;
	xnticks_t timeout, period;
	struct xnthread *thread;

	if (priv->curr == NULL)
		return 0;	/* All done. */

	thread = link2thread(priv->curr, glink);
	priv->curr = nextq(&nkpod->threadq, priv->curr);

	p->cpu = xnsched_cpu(thread->sched);
	p->pid = xnthread_user_pid(thread);
	memcpy(p->name, thread->name, sizeof(p->name));
	p->cprio = thread->cprio;
	p->dnprio = xnthread_get_denormalized_prio(thread, thread->cprio);
	p->state = xnthread_state_flags(thread);
	memcpy(p->timebase, xntbase_name(xnthread_time_base(thread)),
	       sizeof(p->timebase));
	xnobject_copy_name(p->sched_class, thread->sched_class->name);
	period = xnthread_get_period(thread);
	timeout = xnthread_get_timeout(thread, priv->start_time);
	/*
	 * Here we cheat: thread is periodic and the sampling rate may
	 * be high, so it is indeed possible that the next tick date
	 * from the ptimer progresses fast enough while we are busy
	 * collecting output data in this loop, so that next_date -
	 * start_time > period. In such a case, we simply ceil the
	 * value to period to keep the result meaningful, even if not
	 * necessarily accurate. But what does accuracy mean when the
	 * sampling frequency is high, and the way to read it has to
	 * go through the vfile interface anyway?
	 */
	if (period > 0 && period < timeout &&
	    !xntimer_running_p(&thread->rtimer))
		timeout = period;
	p->timeout = timeout;
	p->periodic = xntbase_periodic_p(xnthread_time_base(thread));

	return 1;
}

static int vfile_sched_show(struct xnvfile_snapshot_iterator *it, void *data)
{
	struct vfile_sched_data *p = data;
	char sbuf[64], pbuf[16], tbuf[16];

	if (p == NULL)
		xnvfile_printf(it,
			       "%-3s  %-6s %-5s  %-8s %-8s  %-10s %-10s %s\n",
			       "CPU", "PID", "CLASS", "PRI", "TIMEOUT",
			       "TIMEBASE", "STAT", "NAME");
	else {
		if (p->cprio != p->dnprio)
			snprintf(pbuf, sizeof(pbuf), "%3d(%d)",
				 p->cprio, p->dnprio);
		else
			snprintf(pbuf, sizeof(pbuf), "%3d", p->cprio);

		xntimer_format_time(p->timeout, p->periodic, tbuf, sizeof(tbuf));
		xnthread_format_status(p->state, sbuf, sizeof(sbuf));

		xnvfile_printf(it,
			       "%3u  %-6d %-5s  %-8s %-8s  %-10s %-10s %s\n",
			       p->cpu,
			       p->pid,
			       p->sched_class,
			       pbuf,
			       tbuf,
			       p->timebase,
			       sbuf,
			       p->name);
	}

	return 0;
}

static struct xnvfile_snapshot_ops vfile_sched_ops = {
	.rewind = vfile_sched_rewind,
	.next = vfile_sched_next,
	.show = vfile_sched_show,
};

#ifdef CONFIG_XENO_OPT_STATS

struct vfile_stat_priv {
	int irq;
	struct xnholder *curr;
	struct xnintr_iterator intr_it;
};

struct vfile_stat_data {
	int cpu;
	pid_t pid;
	xnflags_t state;
	char name[XNOBJECT_NAME_LEN];
	unsigned long ssw;
	unsigned long csw;
	unsigned long pf;
	xnticks_t exectime_period;
	xnticks_t account_period;
	xnticks_t exectime_total;
};

static struct xnvfile_snapshot_ops vfile_stat_ops;

static struct xnvfile_snapshot stat_vfile = {
	.privsz = sizeof(struct vfile_stat_priv),
	.datasz = sizeof(struct vfile_stat_data),
	.tag = &nkpod_struct.threadlist_tag,
	.ops = &vfile_stat_ops,
};

static int vfile_stat_rewind(struct xnvfile_snapshot_iterator *it)
{
	struct vfile_stat_priv *priv = xnvfile_iterator_priv(it);
	int irqnr;

	/*
	 * The activity numbers on each valid interrupt descriptor are
	 * grouped under a pseudo-thread.
	 */
	priv->curr = getheadq(&nkpod->threadq);
	priv->irq = 0;
	irqnr = xnintr_query_init(&priv->intr_it) * XNARCH_NR_CPUS;

	return irqnr + countq(&nkpod->threadq);
}

static int vfile_stat_next(struct xnvfile_snapshot_iterator *it, void *data)
{
	struct vfile_stat_priv *priv = xnvfile_iterator_priv(it);
	struct vfile_stat_data *p = data;
	struct xnthread *thread;
	struct xnsched *sched;
	xnticks_t period;
	int ret;

	if (priv->curr == NULL)
		/*
		 * We are done with actual threads, scan interrupt
		 * descriptors.
		 */
		goto scan_irqs;

	thread = link2thread(priv->curr, glink);
	priv->curr = nextq(&nkpod->threadq, priv->curr);

	sched = thread->sched;
	p->cpu = xnsched_cpu(sched);
	p->pid = xnthread_user_pid(thread);
	memcpy(p->name, thread->name, sizeof(p->name));
	p->state = xnthread_state_flags(thread);
	p->ssw = xnstat_counter_get(&thread->stat.ssw);
	p->csw = xnstat_counter_get(&thread->stat.csw);
	p->pf = xnstat_counter_get(&thread->stat.pf);

	period = sched->last_account_switch - thread->stat.lastperiod.start;
	if (period == 0 && thread == sched->curr) {
		p->exectime_period = 1;
		p->account_period = 1;
	} else {
		p->exectime_period = thread->stat.account.total -
			thread->stat.lastperiod.total;
		p->account_period = period;
	}
	p->exectime_total = thread->stat.account.total;
	thread->stat.lastperiod.total = thread->stat.account.total;
	thread->stat.lastperiod.start = sched->last_account_switch;

	return 1;

scan_irqs:
	if (priv->irq >= XNARCH_NR_IRQS)
		return 0;	/* All done. */

	ret = xnintr_query_next(priv->irq, &priv->intr_it, p->name);
	if (ret) {
		if (ret == -EAGAIN)
			xnvfile_touch(it->vfile); /* force rewind. */
		priv->irq++;
		return VFILE_SEQ_SKIP;
	}

	if (!xnarch_cpu_supported(priv->intr_it.cpu))
		return VFILE_SEQ_SKIP;

	p->cpu = priv->intr_it.cpu;
	p->csw = priv->intr_it.hits;
	p->exectime_period = priv->intr_it.exectime_period;
	p->account_period = priv->intr_it.account_period;
	p->exectime_total = priv->intr_it.exectime_total;
	p->pid = 0;
	p->state =  0;
	p->ssw = 0;
	p->pf = 0;

	return 1;
}

static int vfile_stat_show(struct xnvfile_snapshot_iterator *it, void *data)
{
	struct vfile_stat_data *p = data;
	int usage = 0;

	if (p == NULL)
		xnvfile_printf(it,
			       "%-3s  %-6s %-10s %-10s %-4s  %-8s  %5s"
			       "  %s\n",
			       "CPU", "PID", "MSW", "CSW", "PF", "STAT", "%CPU",
			       "NAME");
	else {
		if (p->account_period) {
			while (p->account_period > 0xffffffffUL) {
				p->exectime_period >>= 16;
				p->account_period >>= 16;
			}
			usage = xnarch_ulldiv(p->exectime_period * 1000LL +
					      (p->account_period >> 1),
					      p->account_period, NULL);
		}
		xnvfile_printf(it,
			       "%3u  %-6d %-10lu %-10lu %-4lu  %.8lx  %3u.%u"
			       "  %s\n",
			       p->cpu, p->pid, p->ssw, p->csw, p->pf, p->state,
			       usage / 10, usage % 10, p->name);
	}

	return 0;
}

static int vfile_acct_show(struct xnvfile_snapshot_iterator *it, void *data)
{
	struct vfile_stat_data *p = data;

	if (p == NULL)
		return 0;

	xnvfile_printf(it, "%u %d %lu %lu %lu %.8lx %Lu %Lu %Lu %s\n",
		       p->cpu, p->pid, p->ssw, p->csw, p->pf, p->state,
		       xnarch_tsc_to_ns(p->account_period),
		       xnarch_tsc_to_ns(p->exectime_period),
		       xnarch_tsc_to_ns(p->exectime_total),
		       p->name);

	return 0;
}

static struct xnvfile_snapshot_ops vfile_stat_ops = {
	.rewind = vfile_stat_rewind,
	.next = vfile_stat_next,
	.show = vfile_stat_show,
};

/*
 * An accounting vfile is a thread statistics vfile in disguise with a
 * different output format, which is parser-friendly.
 */
static struct xnvfile_snapshot_ops vfile_acct_ops;

static struct xnvfile_snapshot acct_vfile = {
	.privsz = sizeof(struct vfile_stat_priv),
	.datasz = sizeof(struct vfile_stat_data),
	.tag = &nkpod_struct.threadlist_tag,
	.ops = &vfile_acct_ops,
};

static struct xnvfile_snapshot_ops vfile_acct_ops = {
	.rewind = vfile_stat_rewind,
	.next = vfile_stat_next,
	.show = vfile_acct_show,
};

#endif /* CONFIG_XENO_OPT_STATS */

int xnsched_init_proc(void)
{
	struct xnsched_class *p;
	int ret;

	ret = xnvfile_init_snapshot("sched", &sched_vfile, &nkvfroot);
	if (ret)
		return ret;

	ret = xnvfile_init_dir("schedclasses", &schedclass_vfroot, &nkvfroot);
	if (ret)
		return ret;

	for_each_xnsched_class(p) {
		if (p->sched_init_vfile) {
			ret = p->sched_init_vfile(p, &schedclass_vfroot);
			if (ret)
				return ret;
		}
	}

#ifdef CONFIG_XENO_OPT_STATS
	ret = xnvfile_init_snapshot("stat", &stat_vfile, &nkvfroot);
	if (ret)
		return ret;
	ret = xnvfile_init_snapshot("acct", &acct_vfile, &nkvfroot);
	if (ret)
		return ret;
#endif /* CONFIG_XENO_OPT_STATS */

	return 0;
}

void xnsched_cleanup_proc(void)
{
	struct xnsched_class *p;

	for_each_xnsched_class(p) {
		if (p->sched_cleanup_vfile)
			p->sched_cleanup_vfile(p);
	}

#ifdef CONFIG_XENO_OPT_STATS
	xnvfile_destroy_snapshot(&acct_vfile);
	xnvfile_destroy_snapshot(&stat_vfile);
#endif /* CONFIG_XENO_OPT_STATS */
	xnvfile_destroy_dir(&schedclass_vfroot);
	xnvfile_destroy_snapshot(&sched_vfile);
}

#endif /* CONFIG_XENO_OPT_VFILE */

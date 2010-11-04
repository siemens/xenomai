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
	xnsched_register_class(&xnsched_class_rt);
#ifdef CONFIG_XENO_OPT_SCHED_SPORADIC
	xnsched_register_class(&xnsched_class_sporadic);
#endif
#ifdef CONFIG_XENO_OPT_SCHED_TP
	xnsched_register_class(&xnsched_class_tp);
#endif
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
		xnshadow_call_mayday(thread, SIGDEBUG_WATCHDOG);
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

#ifdef CONFIG_PROC_FS

#include <linux/seq_file.h>

static struct proc_dir_entry *schedclass_proc_root;

struct sched_seq_iterator {
	xnticks_t start_time;
	int nentries;
	struct sched_seq_info {
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
	} sched_info[1];
};

static void *sched_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct sched_seq_iterator *iter = seq->private;

	if (*pos > iter->nentries)
		return NULL;

	if (*pos == 0)
		return SEQ_START_TOKEN;

	return iter->sched_info + *pos - 1;
}

static void *sched_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct sched_seq_iterator *iter = seq->private;

	++*pos;

	if (*pos > iter->nentries)
		return NULL;

	return iter->sched_info + *pos - 1;
}

static void sched_seq_stop(struct seq_file *seq, void *v)
{
}

static int sched_seq_show(struct seq_file *seq, void *v)
{
	char sbuf[64], pbuf[16], tbuf[16];

	if (v == SEQ_START_TOKEN)
		seq_printf(seq, "%-3s  %-6s %-5s  %-8s %-8s  %-10s %-10s %s\n",
			   "CPU", "PID", "CLASS", "PRI", "TIMEOUT", "TIMEBASE", "STAT", "NAME");
	else {
		struct sched_seq_info *p = v;

		if (p->cprio != p->dnprio)
			snprintf(pbuf, sizeof(pbuf), "%3d(%d)",
				 p->cprio, p->dnprio);
		else
			snprintf(pbuf, sizeof(pbuf), "%3d", p->cprio);

		xntimer_format_time(p->timeout, p->periodic, tbuf, sizeof(tbuf));
		xnthread_format_status(p->state, sbuf, sizeof(sbuf));

		seq_printf(seq, "%3u  %-6d %-5s  %-8s %-8s  %-10s %-10s %s\n",
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

static struct seq_operations sched_op = {
	.start = &sched_seq_start,
	.next = &sched_seq_next,
	.stop = &sched_seq_stop,
	.show = &sched_seq_show
};

static int sched_seq_open(struct inode *inode, struct file *file)
{
	struct sched_seq_iterator *iter = NULL;
	xnticks_t period, timeout;
	struct xnholder *holder;
	struct sched_seq_info *p;
	struct seq_file *seq;
	int err, count, rev;
	spl_t s;

	if (!xnpod_active_p())
		return -ESRCH;

	xnlock_get_irqsave(&nklock, s);

      restart:
	rev = nkpod->threadq_rev;
	count = countq(&nkpod->threadq);	/* Cannot be empty (ROOT) */
	holder = getheadq(&nkpod->threadq);

	xnlock_put_irqrestore(&nklock, s);

	if (iter)
		kfree(iter);
	iter = kmalloc(sizeof(*iter)
		       + (count - 1) * sizeof(struct sched_seq_info),
		       GFP_KERNEL);
	if (!iter)
		return -ENOMEM;

	err = seq_open(file, &sched_op);

	if (err) {
		kfree(iter);
		return err;
	}

	iter->nentries = 0;
	iter->start_time = xntbase_get_jiffies(&nktbase);

	/*
	 * Take a snapshot element-wise, restart if something changes
	 * underneath us.
	 */
	while (holder) {
		xnthread_t *thread;
		int n;

		xnlock_get_irqsave(&nklock, s);

		if (nkpod->threadq_rev != rev)
			goto restart;

		rev = nkpod->threadq_rev;
		thread = link2thread(holder, glink);
		n = iter->nentries++;
		p = iter->sched_info + n;

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
		timeout = xnthread_get_timeout(thread, iter->start_time);
		/*
		 * Here we cheat: thread is periodic and the sampling
		 * rate may be high, so it is indeed possible that the
		 * next tick date from the ptimer progresses fast
		 * enough while we are busy collecting output data in
		 * this loop, so that next_date - start_time >
		 * period. In such a case, we simply ceil the value to
		 * period to keep the result meaningful, even if not
		 * necessarily accurate. But what does accuracy mean
		 * when the sampling frequency is high, and the way to
		 * read it has to go through the /proc interface
		 * anyway?
		 */
		if (period > 0 && period < timeout &&
		    !xntimer_running_p(&thread->rtimer))
			timeout = period;
		p->timeout = timeout;
		p->periodic = xntbase_periodic_p(xnthread_time_base(thread));

		holder = nextq(&nkpod->threadq, holder);
		xnlock_put_irqrestore(&nklock, s);
	}

	seq = file->private_data;
	seq->private = iter;

	return 0;
}

static struct file_operations sched_seq_operations = {
	.owner = THIS_MODULE,
	.open = sched_seq_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release_private,
};

#ifdef CONFIG_XENO_OPT_STATS

struct stat_seq_iterator {
	int nentries;
	struct stat_seq_info {
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
	} stat_info[1];
};

static void *stat_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct stat_seq_iterator *iter = seq->private;

	if (*pos > iter->nentries)
		return NULL;

	if (*pos == 0)
		return SEQ_START_TOKEN;

	return iter->stat_info + *pos - 1;
}

static void *stat_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct stat_seq_iterator *iter = seq->private;

	++*pos;

	if (*pos > iter->nentries)
		return NULL;

	return iter->stat_info + *pos - 1;
}

static void stat_seq_stop(struct seq_file *seq, void *v)
{
}

static int __stat_seq_open(struct inode *inode,
			   struct file *file, struct seq_operations *ops)
{
	struct stat_seq_iterator *iter = NULL;
	struct stat_seq_info *stat_info;
	int err, count, thrq_rev, irq;
	xnintr_iterator_t intr_iter;
	struct seq_file *seq;
	xnholder_t *holder;
	int intr_count;
	spl_t s;

	if (!xnpod_active_p())
		return -ESRCH;

      restart_unlocked:
	xnlock_get_irqsave(&nklock, s);

      restart:
	count = countq(&nkpod->threadq);	/* Cannot be empty (ROOT) */
	holder = getheadq(&nkpod->threadq);
	thrq_rev = nkpod->threadq_rev;

	xnlock_put_irqrestore(&nklock, s);

	intr_count = xnintr_query_init(&intr_iter);
	count += intr_count * RTHAL_NR_CPUS;

	if (iter)
		kfree(iter);
	iter = kmalloc(sizeof(*iter)
		       + (count - 1) * sizeof(struct stat_seq_info),
		       GFP_KERNEL);
	if (!iter)
		return -ENOMEM;

	err = seq_open(file, ops);

	if (err) {
		kfree(iter);
		return err;
	}

	iter->nentries = 0;

	/* Take a snapshot element-wise, restart if something changes
	   underneath us. */

	while (holder) {
		xnthread_t *thread;
		xnsched_t *sched;
		xnticks_t period;

		xnlock_get_irqsave(&nklock, s);

		if (nkpod->threadq_rev != thrq_rev)
			goto restart;

		thread = link2thread(holder, glink);
		stat_info = &iter->stat_info[iter->nentries++];

		sched = thread->sched;
		stat_info->cpu = xnsched_cpu(sched);
		stat_info->pid = xnthread_user_pid(thread);
		memcpy(stat_info->name, thread->name,
		       sizeof(stat_info->name));
		stat_info->state = xnthread_state_flags(thread);
		stat_info->ssw = xnstat_counter_get(&thread->stat.ssw);
		stat_info->csw = xnstat_counter_get(&thread->stat.csw);
		stat_info->pf = xnstat_counter_get(&thread->stat.pf);

		period = sched->last_account_switch - thread->stat.lastperiod.start;
		if (!period && thread == sched->curr) {
			stat_info->exectime_period = 1;
			stat_info->account_period = 1;
		} else {
			stat_info->exectime_period = thread->stat.account.total -
				thread->stat.lastperiod.total;
			stat_info->account_period = period;
		}
		stat_info->exectime_total = thread->stat.account.total;
		thread->stat.lastperiod.total = thread->stat.account.total;
		thread->stat.lastperiod.start = sched->last_account_switch;

		holder = nextq(&nkpod->threadq, holder);

		xnlock_put_irqrestore(&nklock, s);
	}

	/* Iterate over all IRQ numbers, ... */
	for (irq = 0; irq < XNARCH_NR_IRQS; irq++)
		/* ...over all shared IRQs on all CPUs */
		while (1) {
			stat_info = &iter->stat_info[iter->nentries];

			err = xnintr_query_next(irq, &intr_iter,
						stat_info->name);
			if (err) {
				if (err == -EAGAIN)
					goto restart_unlocked;
				break; /* line unused or end of chain */
			}

			if (xnarch_cpu_supported(intr_iter.cpu)) {
				stat_info->cpu = intr_iter.cpu;
				stat_info->csw = intr_iter.hits;
				stat_info->exectime_period =
					intr_iter.exectime_period;
				stat_info->account_period =
					intr_iter.account_period;
				stat_info->exectime_total =
					intr_iter.exectime_total;
				stat_info->pid = 0;
				stat_info->state =  0;
				stat_info->ssw = 0;
				stat_info->pf = 0;

				iter->nentries++;
			}
		}

	seq = file->private_data;
	seq->private = iter;

	return 0;
}

static int stat_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN)
		seq_printf(seq, "%-3s  %-6s %-10s %-10s %-4s  %-8s  %5s"
			   "  %s\n",
			   "CPU", "PID", "MSW", "CSW", "PF", "STAT", "%CPU",
			   "NAME");
	else {
		struct stat_seq_info *p = v;
		int usage = 0;

		if (p->account_period) {
			while (p->account_period > 0xFFFFFFFF) {
				p->exectime_period >>= 16;
				p->account_period >>= 16;
			}
			usage =
			    xnarch_ulldiv(p->exectime_period * 1000LL +
					  (p->account_period >> 1),
					  p->account_period, NULL);
		}
		seq_printf(seq, "%3u  %-6d %-10lu %-10lu %-4lu  %.8lx  %3u.%u"
			   "  %s\n",
			   p->cpu, p->pid, p->ssw, p->csw, p->pf, p->state,
			   usage / 10, usage % 10, p->name);
	}

	return 0;
}

static struct seq_operations stat_op = {
	.start = &stat_seq_start,
	.next = &stat_seq_next,
	.stop = &stat_seq_stop,
	.show = &stat_seq_show
};

static int stat_seq_open(struct inode *inode, struct file *file)
{
	return __stat_seq_open(inode, file, &stat_op);
}

static struct file_operations stat_seq_operations = {
	.owner = THIS_MODULE,
	.open = stat_seq_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release_private,
};

static int acct_seq_show(struct seq_file *seq, void *v)
{
	struct stat_seq_info *p;

	if (v == SEQ_START_TOKEN)
		return 0;
	/*
	 * Dump per-thread data.
	 */
	p = v;

	seq_printf(seq, "%u %d %lu %lu %lu %.8lx %Lu %Lu %Lu %s\n",
		   p->cpu, p->pid, p->ssw, p->csw, p->pf, p->state,
		   xnarch_tsc_to_ns(p->account_period), xnarch_tsc_to_ns(p->exectime_period),
		   xnarch_tsc_to_ns(p->exectime_total), p->name);

	return 0;
}

static struct seq_operations acct_op = {
	.start = &stat_seq_start,
	.next = &stat_seq_next,
	.stop = &stat_seq_stop,
	.show = &acct_seq_show
};

static int acct_seq_open(struct inode *inode, struct file *file)
{
	return __stat_seq_open(inode, file, &acct_op);
}

static struct file_operations acct_seq_operations = {
	.owner = THIS_MODULE,
	.open = acct_seq_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release_private,
};

#endif /* CONFIG_XENO_OPT_STATS */

void xnsched_init_proc(void)
{
	struct xnsched_class *p;

	rthal_add_proc_seq("sched", &sched_seq_operations, 0, rthal_proc_root);
	schedclass_proc_root =
		create_proc_entry("schedclasses", S_IFDIR, rthal_proc_root);

	for_each_xnsched_class(p) {
		if (p->sched_init_proc == NULL)
			continue;
		p->proc = create_proc_entry(p->name, S_IFDIR,
					    schedclass_proc_root);
		if (p->proc)
			p->sched_init_proc(p->proc);
	}

#ifdef CONFIG_XENO_OPT_STATS
	rthal_add_proc_seq("stat", &stat_seq_operations, 0, rthal_proc_root);
	rthal_add_proc_seq("acct", &acct_seq_operations, 0, rthal_proc_root);
#endif /* CONFIG_XENO_OPT_STATS */
}

void xnsched_cleanup_proc(void)
{
	struct xnsched_class *p;

	for_each_xnsched_class(p) {
		if (p->proc == NULL)
			continue;
		if (p->sched_cleanup_proc)
			p->sched_cleanup_proc(p->proc);
		remove_proc_entry(p->name, schedclass_proc_root);
	}

#ifdef CONFIG_XENO_OPT_STATS
	remove_proc_entry("acct", rthal_proc_root);
	remove_proc_entry("stat", rthal_proc_root);
#endif /* CONFIG_XENO_OPT_STATS */
	remove_proc_entry("schedclasses", rthal_proc_root);
	remove_proc_entry("sched", rthal_proc_root);
}

#endif /* CONFIG_PROC_FS */

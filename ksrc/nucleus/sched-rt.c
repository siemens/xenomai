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

#ifdef CONFIG_PROC_FS

#include <linux/seq_file.h>

struct xnsched_rt_seq_iterator {
	xnticks_t start_time;
	int nentries;
	struct xnsched_rt_info {
		int cpu;
		pid_t pid;
		char name[XNOBJECT_NAME_LEN];
		xnticks_t period;
		int periodic;
		int cprio;
		int dnprio;
	} sched_info[1];
};

static void *xnsched_rt_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct xnsched_rt_seq_iterator *iter = seq->private;

	if (*pos > iter->nentries)
		return NULL;

	if (*pos == 0)
		return SEQ_START_TOKEN;

	return iter->sched_info + *pos - 1;
}

static void *xnsched_rt_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct xnsched_rt_seq_iterator *iter = seq->private;

	++*pos;

	if (*pos > iter->nentries)
		return NULL;

	return iter->sched_info + *pos - 1;
}

static void xnsched_rt_seq_stop(struct seq_file *seq, void *v)
{
}

static int xnsched_rt_seq_show(struct seq_file *seq, void *v)
{
	char pribuf[16], ptbuf[16];
	struct xnsched_rt_info *p;

	if (v == SEQ_START_TOKEN)
		seq_printf(seq, "%-3s  %-6s %-8s %-10s %s\n",
			   "CPU", "PID", "PRI", "PERIOD", "NAME");
	else {
		p = v;

		if (p->cprio != p->dnprio)
			snprintf(pribuf, sizeof(pribuf), "%3d(%d)",
				 p->cprio, p->dnprio);
		else
			snprintf(pribuf, sizeof(pribuf), "%3d", p->cprio);

		xntimer_format_time(p->period, p->periodic, ptbuf, sizeof(ptbuf));

		seq_printf(seq, "%3u  %-6d %-8s %-10s %s\n",
			   p->cpu,
			   p->pid,
			   pribuf,
			   ptbuf,
			   p->name);
	}

	return 0;
}

static struct seq_operations xnsched_rt_seq_op = {
	.start = &xnsched_rt_seq_start,
	.next = &xnsched_rt_seq_next,
	.stop = &xnsched_rt_seq_stop,
	.show = &xnsched_rt_seq_show
};

static int xnsched_rt_seq_open(struct inode *inode, struct file *file)
{
	struct xnsched_rt_seq_iterator *iter = NULL;
	struct xnsched_rt_info *p;
	struct xnholder *holder;
	struct xnthread *thread;
	int ret, count, rev, n;
	struct seq_file *seq;
	spl_t s;

	if (!xnpod_active_p())
		return -ESRCH;

	xnlock_get_irqsave(&nklock, s);

      restart:
	rev = nkpod->threadq_rev;
	count = xnsched_class_rt.nthreads;
	holder = getheadq(&nkpod->threadq);

	xnlock_put_irqrestore(&nklock, s);

	if (iter)
		kfree(iter);

	if (count == 0)
		return -ESRCH;

	iter = kmalloc(sizeof(*iter)
		       + (count - 1) * sizeof(struct xnsched_rt_info),
		       GFP_KERNEL);
	if (iter == NULL)
		return -ENOMEM;

	ret = seq_open(file, &xnsched_rt_seq_op);
	if (ret) {
		kfree(iter);
		return ret;
	}

	iter->nentries = 0;
	iter->start_time = xntbase_get_jiffies(&nktbase);

	while (holder) {
		xnlock_get_irqsave(&nklock, s);

		if (nkpod->threadq_rev != rev)
			goto restart;

		rev = nkpod->threadq_rev;
		thread = link2thread(holder, glink);

		if (thread->base_class != &xnsched_class_rt)
			goto skip;

		n = iter->nentries++;
		p = iter->sched_info + n;
		p->cpu = xnsched_cpu(thread->sched);
		p->pid = xnthread_user_pid(thread);
		memcpy(p->name, thread->name, sizeof(p->name));
		p->cprio = thread->cprio;
		p->dnprio = xnthread_get_denormalized_prio(thread, thread->cprio);
		p->period = xnthread_get_period(thread);
		p->periodic = xntbase_periodic_p(xnthread_time_base(thread));
	skip:
		holder = nextq(&nkpod->threadq, holder);
		xnlock_put_irqrestore(&nklock, s);
	}

	seq = file->private_data;
	seq->private = iter;

	return 0;
}

static struct file_operations xnsched_rt_seq_operations = {
	.owner = THIS_MODULE,
	.open = xnsched_rt_seq_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release_private,
};

void xnsched_rt_init_proc(struct proc_dir_entry *root)
{
	rthal_add_proc_seq("threads", &xnsched_rt_seq_operations, 0, root);
}

void xnsched_rt_cleanup_proc(struct proc_dir_entry *root)
{
	remove_proc_entry("threads", root);
}

#endif /* CONFIG_PROC_FS */

struct xnsched_class xnsched_class_rt = {

	.sched_init		=	xnsched_rt_init,
	.sched_enqueue		=	xnsched_rt_enqueue,
	.sched_dequeue		=	xnsched_rt_dequeue,
	.sched_requeue		=	xnsched_rt_requeue,
	.sched_pick		=	xnsched_rt_pick,
	.sched_tick		=	xnsched_rt_tick,
	.sched_rotate		=	xnsched_rt_rotate,
	.sched_forget		=	NULL,
	.sched_declare		=	NULL,
	.sched_setparam		=	xnsched_rt_setparam,
	.sched_trackprio	=	xnsched_rt_trackprio,
	.sched_getparam		=	xnsched_rt_getparam,
#ifdef CONFIG_XENO_OPT_PRIOCPL
	.sched_push_rpi 	=	xnsched_rt_push_rpi,
	.sched_pop_rpi		=	xnsched_rt_pop_rpi,
	.sched_peek_rpi 	=	xnsched_rt_peek_rpi,
	.sched_suspend_rpi 	=	NULL,
	.sched_resume_rpi 	=	NULL,
#endif
#ifdef CONFIG_PROC_FS
	.sched_init_proc	=	xnsched_rt_init_proc,
	.sched_cleanup_proc	=	xnsched_rt_cleanup_proc,
#endif
	.weight			=	XNSCHED_CLASS_WEIGHT(1),
	.name			=	"rt"
};
EXPORT_SYMBOL_GPL(xnsched_class_rt);

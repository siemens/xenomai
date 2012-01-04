/*
 * Copyright (C) 2012 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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

#ifndef _XENO_ASM_GENERIC_BITS_SHADOW_H
#define _XENO_ASM_GENERIC_BITS_SHADOW_H

#ifndef __KERNEL__
#error "Pure kernel header included from user-space!"
#endif

#ifdef CONFIG_XENO_LEGACY_IPIPE

struct gatekeeper_data {
	struct task_struct *task_hijacked;
	struct task_struct *gatekeeper;
	struct semaphore gksync;
	struct xnthread *gktarget;
};

static DEFINE_PER_CPU(struct gatekeeper_data, shadow_migration);

#define WORKBUF_SIZE 2048
static DEFINE_PER_CPU_ALIGNED(unsigned char[WORKBUF_SIZE], work_buf);
static DEFINE_PER_CPU(void *, work_tail);

static int lostage_virq;

static void do_lostage_work(unsigned int virq, void *cookie)
{
	struct ipipe_work_header *work;
	unsigned long flags;
	void *curr, *tail;
	int cpu;

	cpu = smp_processor_id();
	curr = per_cpu(work_buf, cpu);

	for (;;) {
		flags = hard_local_irq_save();
		tail = per_cpu(work_tail, cpu);
		if (curr == tail) {
			per_cpu(work_tail, cpu) = per_cpu(work_buf, cpu);
			hard_local_irq_restore(flags);
			return;
		}
		work = curr;
		curr += work->size;
		hard_local_irq_restore(flags);
		work->handler(work);
	}
}

void __ipipe_post_work_root(struct ipipe_work_header *work)
{
	unsigned long flags;
	void *tail;
	int cpu;

	flags = ipipe_test_and_stall_head();
	tail = per_cpu(work_tail, cpu);
	cpu = ipipe_processor_id();

	if (WARN_ON_ONCE((unsigned char *)tail + work->size >=
			 per_cpu(work_buf, cpu) + WORKBUF_SIZE))
		goto out;

	memcpy(tail, work, work->size);
	per_cpu(work_tail, cpu) = tail + work->size;
	ipipe_post_irq_root(lostage_virq);
out:
	ipipe_restore_head(flags);
}

static inline void __ipipe_reenter_root(void)
{
	struct task_struct *prev;
	int policy, prio, cpu;

	cpu = task_cpu(current);
	policy = current->rt_priority ? SCHED_FIFO : SCHED_NORMAL;
	prio = current->rt_priority;
	prev = per_cpu(shadow_migration, cpu).task_hijacked;

	ipipe_reenter_root(prev, policy, prio);
}

static int gatekeeper_thread(void *data)
{
	struct sched_param param = { .sched_priority = MAX_RT_PRIO - 1 };
	struct xnthread *target;
	struct task_struct *p;
	struct xnsched *sched;
	int cpu = (long)data;
	cpumask_t cpumask;
	spl_t s;

	p = current;
	sched = xnpod_sched_slot(cpu);
	p->flags |= PF_NOFREEZE;
	sigfillset(&p->blocked);
	cpumask = cpumask_of_cpu(cpu);
	set_cpus_allowed(p, cpumask);
	sched_setscheduler_nocheck(p, SCHED_FIFO, &param);

	set_current_state(TASK_INTERRUPTIBLE);
	/* Sync with __xnshadow_init(). */
	up(&per_cpu(shadow_migration, cpu).gksync);

	for (;;) {
		/* Make the request token available. */
		up(&per_cpu(shadow_migration, cpu).gksync);
		schedule();

		if (kthread_should_stop())
			break;

		/*
		 * Real-time shadow TCBs are always removed on behalf
		 * of the killed thread.
		 */
		target = per_cpu(shadow_migration, cpu).gktarget;

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

static inline void __xnshadow_init(void)
{
	struct gatekeeper_data *gd;
	int key, cpu;

	key = ipipe_alloc_ptdkey();
	/* In emulation mode, we want PTD key #0, no matter what. */
	BUG_ON(key != 0);

	lostage_virq = ipipe_alloc_virq();
	BUG_ON(lostage_virq == 0);

	for_each_online_cpu(cpu)
		per_cpu(work_tail, cpu) = per_cpu(work_buf, cpu);

	ipipe_request_irq(ipipe_root_domain, lostage_virq,
			  do_lostage_work, NULL, NULL);

	for_each_online_cpu(cpu) {
		gd = &per_cpu(shadow_migration, cpu);
		if (!xnarch_cpu_supported(cpu)) {
			gd->gatekeeper = NULL;
			continue;
		}
		sema_init(&gd->gksync, 0);
		xnarch_memory_barrier();
		gd->gatekeeper = kthread_create(gatekeeper_thread,
						(void *)(long)cpu,
						"gatekeeper/%d", cpu);
		wake_up_process(gd->gatekeeper);
		down(&gd->gksync);
	}
}

static inline void __xnshadow_exit(void)
{
	struct gatekeeper_data *gd;
	int cpu;

	for_each_online_cpu(cpu) {
		gd = &per_cpu(shadow_migration, cpu);
		if (gd->gatekeeper) {
			down(&gd->gksync);
			gd->gktarget = NULL;
			kthread_stop(gd->gatekeeper);
		}
	}

	ipipe_free_irq(ipipe_root_domain, lostage_virq);
	ipipe_free_virq(lostage_virq);
	ipipe_free_ptdkey(0);
}

static inline void set_ptd(struct ipipe_threadinfo *p)
{
	current->ptd[0] = p;
}

static inline void clear_ptd(void)
{
	current->ptd[0] = NULL;
}

#else /* !CONFIG_XENO_LEGACY_IPIPE */

static inline void __xnshadow_init(void) { }

static inline void __xnshadow_exit(void) { }

#define set_ptd(p)  do { } while (0)

static inline void clear_ptd(void) { }

#endif /* !CONFIG_XENO_LEGACY_IPIPE */

#endif /* !_XENO_ASM_GENERIC_BITS_SHADOW_H */

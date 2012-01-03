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

static inline void __xnshadow_init(void)
{
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
}

static inline void __xnshadow_exit(void)
{
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

static inline void hijack_current(void)
{ 
	int cpu = task_cpu(current);

	rthal_archdata.task_hijacked[cpu] = current;
	schedule();
}

#else /* !CONFIG_XENO_LEGACY_IPIPE */

static inline void __xnshadow_init(void) { }

static inline void __xnshadow_exit(void) { }

#define set_ptd(p)  do { } while (0)

static inline void clear_ptd(void) { }

static inline void hijack_current(void)
{ 
	schedule();
}

#endif /* !CONFIG_XENO_LEGACY_IPIPE */

#endif /* !_XENO_ASM_GENERIC_BITS_SHADOW_H */

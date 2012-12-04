/*
 * Copyright (C) 2009 Philippe Gerum <rpm@xenomai.org>.
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

#include <linux/sched.h>
#include <linux/ipipe.h>
#include <nucleus/pod.h>
#include <nucleus/heap.h>
#include <asm/xenomai/thread.h>

void __asm_thread_switch(struct thread_struct *prev,
			 struct thread_struct *next,
			 int kthreadp);

asmlinkage void __asm_thread_trampoline(void);

void xnarch_init_shadow_tcb(struct xnarchtcb *tcb,
			    struct xnthread *thread, const char *name)
{
	struct task_struct *task = current;

	tcb->user_task = task;
	tcb->active_task = NULL;
	tcb->tsp = &task->thread;
	tcb->mm = task->mm;
	tcb->active_mm = NULL;
	tcb->entry = NULL;
	tcb->cookie = NULL;
	tcb->self = thread;
	tcb->imask = 0;
	tcb->name = name;
}

void xnarch_init_root_tcb(struct xnarchtcb *tcb,
			  struct xnthread *thread, const char *name)
{
	tcb->user_task = current;
	tcb->active_task = current;
	tcb->tsp = &tcb->ts;
	tcb->mm = current->mm;
	tcb->active_mm = NULL;
	tcb->entry = NULL;
	tcb->cookie = NULL;
	tcb->self = thread;
	tcb->imask = 0;
	tcb->name = name;
}

void xnarch_init_tcb(struct xnarchtcb *tcb)
{
	tcb->user_task = NULL;
	tcb->active_task = NULL;
	tcb->tsp = &tcb->ts;
	tcb->mm = NULL;
	tcb->active_mm = NULL;
	memset(&tcb->ts, 0, sizeof(tcb->ts));
}

void xnarch_leave_root(struct xnarchtcb *rootcb)
{
	struct task_struct *p = current;

	/* Remember the preempted Linux task pointer. */
	rootcb->user_task = rootcb->active_task = p;
	rootcb->tsp = &p->thread;
	rootcb->mm = rootcb->active_mm = ipipe_get_active_mm();
}

void xnarch_switch_to(struct xnarchtcb *out_tcb, struct xnarchtcb *in_tcb)
{
	struct mm_struct *prev_mm = out_tcb->active_mm, *next_mm;
	struct task_struct *prev = out_tcb->active_task;
	struct task_struct *next = in_tcb->user_task;

	if (likely(next)) {
		in_tcb->active_task = next;
		in_tcb->active_mm = in_tcb->mm;
		ipipe_clear_foreign_stack(&xnarch_machdata.domain);
	} else {
		in_tcb->active_task = prev;
		in_tcb->active_mm = prev_mm;
		ipipe_set_foreign_stack(&xnarch_machdata.domain);
	}

	next_mm = in_tcb->active_mm;

	if (next_mm && likely(prev_mm != next_mm))
		__switch_mm(prev_mm, next_mm, next);

	__asm_thread_switch(out_tcb->tsp, in_tcb->tsp, next == NULL);
}

asmlinkage static void thread_trampoline(struct xnarchtcb *tcb)
{
	xnpod_welcome_thread(tcb->self, tcb->imask);
	tcb->entry(tcb->cookie);
	xnpod_delete_thread(tcb->self);
}

void xnarch_init_thread(struct xnarchtcb *tcb,
			void (*entry)(void *),
			void *cookie, int imask,
			struct xnthread *thread, char *name)
{
	register long gp __asm__ ("gp");

	struct tramp_stack {
		struct switch_stack sw;
		unsigned long r4; /* to hold tcb pointer arg */
		unsigned long ra; /* to xnarch_thread_trampoline() */
	} *childregs;

	childregs = (struct tramp_stack *)
		((unsigned long)tcb->stackbase + tcb->stacksize - sizeof(*childregs));
	/*
	 * Stack space is guaranteed to be clean, so no need to zero
	 * it again.
	 */
	childregs->sw.gp = gp;	/* Inherit GP */
	childregs->sw.ra = (unsigned long)__asm_thread_trampoline;
	childregs->ra = (unsigned long)thread_trampoline;
	childregs->r4 = (unsigned long)tcb;

	tcb->ts.ksp = (unsigned long)childregs;
	tcb->ts.kpsr = 0;	/* PIE=0, U=0, EH=0 */
	tcb->entry = entry;
	tcb->cookie = cookie;
	tcb->self = thread;
	tcb->imask = imask;
	tcb->name = name;
}

int xnarch_escalate(void)
{
	if (ipipe_root_p) {
		ipipe_raise_irq(xnarch_machdata.escalate_virq);
		return 1;
	}

	return 0;
}

int xnarch_alloc_stack(struct xnarchtcb *tcb, size_t stacksize)
{
	int ret = 0;

	tcb->stacksize = stacksize;
	if (stacksize == 0)
		tcb->stackbase = NULL;
	else {
		tcb->stackbase = xnmalloc(stacksize);
		if (tcb->stackbase == NULL)
			ret = -ENOMEM;
	}

	return ret;
}

void xnarch_free_stack(struct xnarchtcb *tcb)
{
	if (tcb->stackbase)
		xnfree(tcb->stackbase);
}

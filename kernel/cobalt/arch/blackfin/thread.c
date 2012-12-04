/*
 * Copyright (C) 2005, 2012 Philippe Gerum <rpm@xenomai.org>.
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
#include <linux/mm.h>
#include <nucleus/pod.h>
#include <nucleus/heap.h>

asmlinkage struct task_struct *
__asm_switch_context(struct thread_struct *prev,
		     struct thread_struct *next);

asmlinkage void __asm_thread_trampoline(void);

asmlinkage int __asm_defer_switch_p(void);

#ifdef CONFIG_MPU

static inline
struct task_struct *mpu_get_prev(struct xnarchtcb *tcb)
{
	return tcb->active_task;
}

static inline
void mpu_set_next(struct xnarchtcb *tcb, struct task_struct *next)
{
	tcb->active_task = next;
}

static inline
void mpu_switch(struct task_struct *prev, struct task_struct *next)
{
	struct mm_struct *oldmm;

	if (next && next != prev) {
		oldmm = prev->active_mm;
		switch_mm(oldmm, next->active_mm, next);
	}
}

#else /* !CONFIG_MPU */

static inline
struct task_struct *mpu_get_prev(struct xnarchtcb *tcb)
{
	return NULL;
}

static inline
void mpu_set_next(struct xnarchtcb *tcb, struct task_struct *next)
{
}

static inline
void mpu_switch(struct task_struct *prev, struct task_struct *next)
{
}

#endif /* CONFIG_MPU */

void xnarch_switch_to(struct xnarchtcb *out_tcb, struct xnarchtcb *in_tcb)
{
	struct task_struct *prev = mpu_get_prev(out_tcb);
	struct task_struct *next = in_tcb->user_task;

	if (likely(next != NULL)) {
		mpu_set_next(in_tcb, next);
		ipipe_clear_foreign_stack(&xnarch_machdata.domain);
	} else {
		mpu_set_next(in_tcb, prev);
		ipipe_set_foreign_stack(&xnarch_machdata.domain);
	}

	mpu_switch(prev, next);

	__asm_switch_context(out_tcb->tsp, in_tcb->tsp);
}

asmlinkage static void thread_trampoline(struct xnarchtcb *tcb)
{
	xnpod_welcome_thread(tcb->self, tcb->imask);
	tcb->entry(tcb->cookie);
	xnpod_delete_thread(tcb->self);
}

void xnarch_init_thread(struct xnarchtcb *tcb,
			void (*entry)(void *),
			void *cookie,
			int imask,
			struct xnthread *thread, char *name)
{
	unsigned long ksp, *switchregs;

	ksp = (((unsigned long)tcb->stackbase + tcb->stacksize - 40) & ~0xf);
	switchregs = (unsigned long *)ksp;
	/*
	 * Stack space is guaranteed to be clear, so R7:4, P5:3, fp
	 * are already zero. We only need to set r0 and rets.
	 */
	switchregs[0] = (unsigned long)tcb;	/* r0 */
	switchregs[9] = (unsigned long)thread_trampoline; /* rets */

	tcb->ts.ksp = ksp;
	tcb->ts.pc = (unsigned long)__asm_thread_trampoline;
	tcb->ts.usp = 0;

	tcb->entry = entry;
	tcb->cookie = cookie;
	tcb->self = thread;
	tcb->imask = imask;
	tcb->name = name;
}

void xnarch_leave_root(struct xnarchtcb *rootcb)
{
	/* Remember the preempted Linux task pointer. */
	rootcb->user_task = current;
#ifdef CONFIG_MPU
	rootcb->active_task = current;
#endif
	rootcb->tsp = &current->thread;
}

int xnarch_escalate(void)
{
	/* The following Blackfin-specific check is likely the most
	 * braindamage stuff we need to do for this arch, i.e. deferring
	 * Xenomai's rescheduling procedure whenever:

	 * 1. ILAT tells us that a deferred syscall (EVT15) is
	 * pending, so that we don't later execute this syscall over
	 * the wrong thread context. This could happen whenever a
	 * user-space task (plain or Xenomai) gets preempted by a high
	 * priority interrupt right after the deferred syscall event
	 * is raised (EVT15) but before the evt_system_call ISR could
	 * run. In case of deferred Xenomai rescheduling, the pending
	 * rescheduling opportunity will be checked at the beginning
	 * of Xenomai's handle_head_syscall() which intercepts any
	 * incoming syscall, and we know it will happen shortly after.
	 *
	 * 2. the context we will switch back to belongs to the Linux
	 * kernel code, so that we don't inadvertently cause the CPU
	 * to switch to user operating mode as a result of returning
	 * from an interrupt stack frame over the incoming thread
	 * through RTI. In the latter case, the preempted kernel code
	 * will be diverted shortly before resumption in order to run
	 * the rescheduling procedure (see __ipipe_irq_tail_hook).
	 */

	if (__asm_defer_switch_p()) {
		__ipipe_lock_root();
		return 1;
	}

	if (ipipe_root_p) {
		ipipe_raise_irq(xnarch_machdata.escalate_virq);
		__ipipe_unlock_root();
		return 1;
	}

	__ipipe_unlock_root();

	return 0;
}

void xnarch_init_root_tcb(struct xnarchtcb *tcb,
			  struct xnthread *thread,
			  const char *name)
{
	tcb->user_task = current;
#ifdef CONFIG_MPU
	tcb->active_task = NULL;
#endif
	tcb->tsp = &tcb->ts;
	tcb->entry = NULL;
	tcb->cookie = NULL;
	tcb->self = thread;
	tcb->imask = 0;
	tcb->name = name;
}

void xnarch_init_shadow_tcb(struct xnarchtcb *tcb,
			    struct xnthread *thread,
			    const char *name)
{
	struct task_struct *task = current;

	tcb->user_task = task;
#ifdef CONFIG_MPU
	tcb->active_task = NULL;
#endif
	tcb->tsp = &task->thread;
	tcb->entry = NULL;
	tcb->cookie = NULL;
	tcb->self = thread;
	tcb->imask = 0;
	tcb->name = name;
}

void xnarch_init_tcb(struct xnarchtcb *tcb)
{

	tcb->user_task = NULL;
#ifdef CONFIG_MPU
	tcb->active_task = NULL;
#endif
	tcb->tsp = &tcb->ts;
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

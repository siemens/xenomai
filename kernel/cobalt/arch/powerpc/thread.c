/*
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
 *
 * 64-bit PowerPC adoption
 *   copyright (C) 2005 Taneli Vähäkangas and Heikki Lindholm
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
#include <asm/mmu_context.h>
#include <nucleus/pod.h>
#include <nucleus/heap.h>

#ifdef CONFIG_PPC64

asmlinkage struct task_struct *
__asm_thread_switch(struct thread_struct *prev_t,
		    struct thread_struct *next_t,
		    int kthreadp);

/* from process.c/copy_thread */
static unsigned long get_stack_vsid(unsigned long ksp)
{
	unsigned long vsid;
	unsigned long llp = mmu_psize_defs[mmu_linear_psize].sllp;

	if (mmu_has_feature(MMU_FTR_1T_SEGMENT))
		vsid = get_kernel_vsid(ksp, MMU_SEGSIZE_1T)
			<< SLB_VSID_SHIFT_1T;
	else
		vsid = get_kernel_vsid(ksp, MMU_SEGSIZE_256M)
			<< SLB_VSID_SHIFT;

	vsid |= SLB_VSID_KERNEL | llp;

	return vsid;
}

#else /* !CONFIG_PPC64 */

asmlinkage struct task_struct *
__asm_thread_switch(struct thread_struct *prev,
		    struct thread_struct *next);

#endif /* !CONFIG_PPC64 */

asmlinkage void __asm_thread_trampoline(void);

void xnarch_enter_root(struct xnarchtcb *rootcb)
{
#ifdef CONFIG_XENO_HW_UNLOCKED_SWITCH
	if (!rootcb->mm)
		set_ti_thread_flag(rootcb->tip, TIF_MMSWITCH_INT);
#endif
	ipipe_unmute_pic();
}

void xnarch_switch_to(struct xnarchtcb *out_tcb,
		      struct xnarchtcb *in_tcb)
{
	struct mm_struct *prev_mm = out_tcb->active_mm, *next_mm;
	struct task_struct *prev = out_tcb->active_task;
	struct task_struct *next = in_tcb->user_task;

	if (likely(next != NULL)) {
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

#ifdef CONFIG_PPC64
	__asm_thread_switch(out_tcb->tsp, in_tcb->tsp, next == NULL);
#else
	__asm_thread_switch(out_tcb->tsp, in_tcb->tsp);
#endif
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
	struct pt_regs *childregs;
	unsigned long sp;

	/*
	 * Stack space is guaranteed to have been fully zeroed. We do
	 * this earlier in xnthread_init() which runs with interrupts
	 * on, to reduce latency.
	 */
	sp = (unsigned long)tcb->stackbase + tcb->stacksize;
	sp -= sizeof(struct pt_regs);
	childregs = (struct pt_regs *)sp;
	sp -= STACK_FRAME_OVERHEAD;

	tcb->ts.ksp = sp;
	tcb->entry = entry;
	tcb->cookie = cookie;
	tcb->self = thread;
	tcb->imask = imask;
	tcb->name = name;

#ifdef CONFIG_PPC64
	childregs->nip = ((unsigned long *)__asm_thread_trampoline)[0];
	childregs->gpr[2] = ((unsigned long *)__asm_thread_trampoline)[1];
	childregs->gpr[22] = (unsigned long)tcb;
	childregs->gpr[23] = ((unsigned long *)thread_trampoline)[0];	/* lr = entry addr. */
	childregs->gpr[24] = ((unsigned long *)thread_trampoline)[1];	/* r2 = TOC base. */
	if (mmu_has_feature(MMU_FTR_SLB))
		tcb->ts.ksp_vsid = get_stack_vsid(tcb->ts.ksp);
#else /* !CONFIG_PPC64 */
	childregs->nip = (unsigned long)__asm_thread_trampoline;
	childregs->gpr[22] = (unsigned long)tcb;
	childregs->gpr[23] = (unsigned long)thread_trampoline;
#endif	/* !CONFIG_PPC64 */
}

#ifdef CONFIG_XENO_HW_FPU

asmlinkage void __asm_init_fpu(struct thread_struct *ts);

asmlinkage void __asm_save_fpu(struct thread_struct *ts);

asmlinkage void __asm_restore_fpu(struct thread_struct *ts);

#ifndef CONFIG_SMP
#define get_fpu_owner(cur) last_task_used_math
#else /* CONFIG_SMP */
#define get_fpu_owner(cur) ({					\
    struct task_struct * _cur = (cur);                          \
    ((_cur->thread.regs && (_cur->thread.regs->msr & MSR_FP))   \
     ? _cur : NULL);                                            \
})
#endif /* CONFIG_SMP */

#ifdef CONFIG_PPC64
#define do_disable_fpu() ({				\
    register unsigned long _msr;                        \
    __asm__ __volatile__ ( "mfmsr %0" : "=r"(_msr) );   \
    __asm__ __volatile__ ( "mtmsrd %0"                  \
			   : /* no output */            \
			   : "r"(_msr & ~(MSR_FP))      \
			   : "memory" );                \
})

#define do_enable_fpu() ({				\
    register unsigned long _msr;                        \
    __asm__ __volatile__ ( "mfmsr %0" : "=r"(_msr) );   \
    __asm__ __volatile__ ( "mtmsrd %0"                  \
			   : /* no output */            \
			   : "r"(_msr | MSR_FP)         \
			   : "memory" );                \
})
#else /* !CONFIG_PPC64 */
#define do_disable_fpu() ({				\
    register unsigned long _msr;                        \
    __asm__ __volatile__ ( "mfmsr %0" : "=r"(_msr) );   \
    __asm__ __volatile__ ( "mtmsr %0"                   \
			   : /* no output */            \
			   : "r"(_msr & ~(MSR_FP))      \
			   : "memory" );                \
})

#define do_enable_fpu() ({				\
    register unsigned long _msr;                        \
    __asm__ __volatile__ ( "mfmsr %0" : "=r"(_msr) );   \
    __asm__ __volatile__ ( "mtmsr %0"                   \
			   : /* no output */            \
			   : "r"(_msr | MSR_FP)         \
			   : "memory" );                \
})
#endif /* CONFIG_PPC64 */

void xnarch_init_fpu(struct xnarchtcb *tcb)
{
	/*
	 * Initialize the FPU for an emerging kernel-based RT
	 * thread. This must be run on behalf of the emerging thread.
	 * xnarch_init_tcb() guarantees that all FPU regs are zeroed
	 * in tcb.
	 */
	__asm_init_fpu(&tcb->ts);
}

void xnarch_enable_fpu(struct xnarchtcb *tcb)
{
	struct task_struct *task = tcb->user_task;

	if (task && task != tcb->user_fpu_owner)
		do_disable_fpu();
	else
		do_enable_fpu();
}

void xnarch_save_fpu(struct xnarchtcb *tcb)
{
	if (tcb->fpup) {
		__asm_save_fpu(tcb->fpup);

		if (tcb->user_fpu_owner &&
		    tcb->user_fpu_owner->thread.regs)
			tcb->user_fpu_owner->thread.regs->msr &= ~(MSR_FP|MSR_FE0|MSR_FE1);
	}
}

void xnarch_restore_fpu(struct xnarchtcb *tcb)
{
	struct thread_struct *ts;
	struct pt_regs *regs;

	if (tcb->fpup) {
		__asm_restore_fpu(tcb->fpup);
		/*
		 * Note: Only enable FP in MSR, if it was enabled when
		 * we saved the fpu state.
		 */
		if (tcb->user_fpu_owner) {
			ts = &tcb->user_fpu_owner->thread;
			regs = ts->regs;
			if (regs) {
				regs->msr &= ~(MSR_FE0|MSR_FE1);
				regs->msr |= (MSR_FP|ts->fpexc_mode);
			}
		}
	}
	/*
	 * FIXME: We restore FPU "as it was" when Xenomai preempted Linux,
	 * whereas we could be much lazier.
	 */
	if (tcb->user_task && tcb->user_task != tcb->user_fpu_owner)
		do_disable_fpu();
}

#endif /* CONFIG_XENO_HW_FPU */

void xnarch_leave_root(struct xnarchtcb *rootcb)
{
	struct task_struct *p = current;

	ipipe_mute_pic();
	/* Remember the preempted Linux task pointer. */
	rootcb->user_task = rootcb->active_task = p;
	rootcb->tsp = &p->thread;
	rootcb->mm = rootcb->active_mm = ipipe_get_active_mm();
#ifdef CONFIG_XENO_HW_UNLOCKED_SWITCH
	rootcb->tip = task_thread_info(p);
#endif
#ifdef CONFIG_XENO_HW_FPU
	rootcb->user_fpu_owner = get_fpu_owner(rootcb->user_task);
	/* So that xnarch_save_fpu() will operate on the right FPU area. */
	rootcb->fpup = rootcb->user_fpu_owner ?
		&rootcb->user_fpu_owner->thread : NULL;
#endif /* CONFIG_XENO_HW_FPU */
}

int xnarch_escalate(void)
{
	if (ipipe_root_p) {
		ipipe_raise_irq(xnarch_machdata.escalate_virq);
		return 1;
	}

	return 0;
}

void xnarch_init_tcb(struct xnarchtcb *tcb)
{
	tcb->user_task = NULL;
	tcb->active_task = NULL;
	tcb->tsp = &tcb->ts;
	tcb->mm = NULL;
	tcb->active_mm = NULL;
#ifdef CONFIG_XENO_HW_UNLOCKED_SWITCH
	tcb->tip = &tcb->ti;
#endif
	/* Note: .pgdir(ppc32) == NULL for a Xenomai kthread. */
	memset(&tcb->ts, 0, sizeof(tcb->ts));
#ifdef CONFIG_XENO_HW_FPU
	tcb->user_fpu_owner = NULL;
	tcb->fpup = &tcb->ts;
#endif /* CONFIG_XENO_HW_FPU */
	/* Must be followed by xnarch_init_thread(). */
}

void xnarch_init_root_tcb(struct xnarchtcb *tcb,
			  struct xnthread *thread,
			  const char *name)
{
	tcb->user_task = current;
	tcb->active_task = NULL;
	tcb->tsp = &tcb->ts;
	tcb->mm = current->mm;
	tcb->active_mm = NULL;
#ifdef CONFIG_XENO_HW_UNLOCKED_SWITCH
	tcb->tip = &tcb->ti;
#endif
#ifdef CONFIG_XENO_HW_FPU
	tcb->user_fpu_owner = NULL;
	tcb->fpup = NULL;
#endif /* CONFIG_XENO_HW_FPU */
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
	tcb->active_task = NULL;
	tcb->tsp = &task->thread;
	tcb->mm = task->mm;
	tcb->active_mm = NULL;
#ifdef CONFIG_XENO_HW_UNLOCKED_SWITCH
	tcb->tip = task_thread_info(task);
#endif
#ifdef CONFIG_XENO_HW_FPU
	tcb->user_fpu_owner = task;
	tcb->fpup = &task->thread;
#endif /* CONFIG_XENO_HW_FPU */
	tcb->entry = NULL;
	tcb->cookie = NULL;
	tcb->self = thread;
	tcb->imask = 0;
	tcb->name = name;
}

int xnarch_alloc_stack(struct xnarchtcb *tcb, size_t stacksize)
{
	int ret = 0;

	tcb->stacksize = stacksize;
	if (stacksize == 0)
		tcb->stackbase = NULL;
	else {
		tcb->stackbase = xnheap_alloc(&kstacks, stacksize);
		if (tcb->stackbase == NULL)
			ret = -ENOMEM;
	}

	return ret;
}

void xnarch_free_stack(struct xnarchtcb *tcb)
{
	if (tcb->stackbase)
		xnheap_free(&kstacks, tcb->stackbase);
}

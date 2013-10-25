/*
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
 *
 * ARM port
 *   Copyright (C) 2005 Stelian Pop
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
#include <cobalt/kernel/thread.h>

asmlinkage void __asm_thread_switch(struct thread_info *out,
				    struct thread_info *in);

asmlinkage void __asm_thread_trampoline(void);

#ifdef CONFIG_XENO_HW_FPU

#define task_fpenv(task)						\
	((struct arm_fpustate *) &task_thread_info(task)->used_cp[0])

#ifdef CONFIG_VFP
asmlinkage void __asm_vfp_save(union vfp_state *vfp, unsigned int fpexc);

asmlinkage void __asm_vfp_load(union vfp_state *vfp, unsigned int cpu);

static inline void do_save_fpu(struct arm_fpustate *fpuenv, unsigned int fpexc)
{
	__asm_vfp_save(&fpuenv->vfpstate, fpexc);
}

static inline void do_restore_fpu(struct arm_fpustate *fpuenv)
{
	__asm_vfp_load(&fpuenv->vfpstate, ipipe_processor_id());
}

#define do_vfp_fmrx(_vfp_)						\
	({								\
		u32 __v;						\
		asm volatile("mrc p10, 7, %0, " __stringify(_vfp_)	\
			     ", cr0, 0 @ fmrx %0, " #_vfp_:		\
			     "=r" (__v));				\
		__v;							\
	})

#define do_vfp_fmxr(_vfp_,_var_)				\
	asm volatile("mcr p10, 7, %0, " __stringify(_vfp_)	\
		     ", cr0, 0 @ fmxr " #_vfp_ ", %0":		\
		     /* */ : "r" (_var_))

extern union vfp_state *vfp_current_hw_state[NR_CPUS];

static inline struct arm_fpustate *get_fpu_owner(void)
{
	union vfp_state *vfp_owner;
	unsigned int cpu;
#ifdef CONFIG_SMP
	unsigned int fpexc;

	fpexc = do_vfp_fmrx(FPEXC);
	if (!(fpexc & FPEXC_EN))
		return NULL;
#endif

	cpu = ipipe_processor_id();
	vfp_owner = vfp_current_hw_state[cpu];
	if (!vfp_owner)
		return NULL;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)			\
     || defined(CONFIG_VFP_3_2_BACKPORT)) && defined(CONFIG_SMP)
	if (vfp_owner->hard.cpu != cpu)
		return NULL;
#endif /* linux >= 3.2.0 */

	return container_of(vfp_owner, struct arm_fpustate, vfpstate);
}

#define do_disable_fpu()					\
	do_vfp_fmxr(FPEXC, do_vfp_fmrx(FPEXC) & ~FPEXC_EN)

#define XNARCH_VFP_ANY_EXC						\
	(FPEXC_EX|FPEXC_DEX|FPEXC_FP2V|FPEXC_VV|FPEXC_TRAP_MASK)

#define do_enable_fpu()							\
	({								\
		unsigned _fpexc = do_vfp_fmrx(FPEXC) | FPEXC_EN;	\
		do_vfp_fmxr(FPEXC, _fpexc & ~XNARCH_VFP_ANY_EXC);	\
		_fpexc;							\
	})

#else /* !CONFIG_VFP */

static inline void do_save_fpu(struct arm_fpustate *fpuenv) { }

static inline void do_restore_fpu(struct arm_fpustate *fpuenv) { }

#define get_fpu_owner(cur)						\
	({								\
		struct task_struct * _cur = (cur);			\
		((task_thread_info(_cur)->used_cp[1] | task_thread_info(_cur)->used_cp[2]) \
		 ? _cur : NULL);					\
	})

#define do_disable_fpu()						\
	task_thread_info(current)->used_cp[1] = task_thread_info(current)->used_cp[2] = 0;

#define do_enable_fpu()							\
	task_thread_info(current)->used_cp[1] = task_thread_info(current)->used_cp[2] = 1;

#endif /* !CONFIG_VFP */

int xnarch_fault_fpu_p(struct ipipe_trap_data *d)
{
	/* This function does the same thing to decode the faulting instruct as
	   "call_fpe" in arch/arm/entry-armv.S */
	static unsigned copro_to_exc[16] = {
		IPIPE_TRAP_UNDEFINSTR,
		/* FPE */
		IPIPE_TRAP_FPU, IPIPE_TRAP_FPU,
		IPIPE_TRAP_UNDEFINSTR,
#ifdef CONFIG_CRUNCH
		IPIPE_TRAP_FPU, IPIPE_TRAP_FPU, IPIPE_TRAP_FPU,
#else /* !CONFIG_CRUNCH */
		IPIPE_TRAP_UNDEFINSTR, IPIPE_TRAP_UNDEFINSTR, IPIPE_TRAP_UNDEFINSTR,
#endif /* !CONFIG_CRUNCH */
		IPIPE_TRAP_UNDEFINSTR, IPIPE_TRAP_UNDEFINSTR, IPIPE_TRAP_UNDEFINSTR,
#ifdef CONFIG_VFP
		IPIPE_TRAP_VFP, IPIPE_TRAP_VFP,
#else /* !CONFIG_VFP */
		IPIPE_TRAP_UNDEFINSTR, IPIPE_TRAP_UNDEFINSTR,
#endif /* !CONFIG_VFP */
		IPIPE_TRAP_UNDEFINSTR, IPIPE_TRAP_UNDEFINSTR,
		IPIPE_TRAP_UNDEFINSTR, IPIPE_TRAP_UNDEFINSTR,
	};
	unsigned instr, exc, cp;
	char *pc;

	if (d->exception == IPIPE_TRAP_FPU)
		return 1;

#ifdef CONFIG_VFP
	if (d->exception == IPIPE_TRAP_VFP)
		goto trap_vfp;
#endif

	/*
	 * When an FPU fault occurs in user-mode, it will be properly
	 * resolved before __ipipe_report_trap() is called.
	 */
	if (d->exception != IPIPE_TRAP_UNDEFINSTR || user_mode(d->regs))
		return 0;

	pc = (char *) xnarch_fault_pc(d);
	if (unlikely(thumb_mode(d->regs))) {
		unsigned short thumbh, thumbl;

#if defined(CONFIG_ARM_THUMB) && __LINUX_ARM_ARCH__ >= 6 && defined(CONFIG_CPU_V7)
#if __LINUX_ARM_ARCH__ < 7
		if (cpu_architecture() < CPU_ARCH_ARMv7)
#else
		if (0)
#endif /* arch < 7 */
#endif /* thumb && arch >= 6 && cpu_v7 */
			return 0;

		thumbh = *(unsigned short *) pc;
		thumbl = *((unsigned short *) pc + 1);

		if ((thumbh & 0x0000f800) < 0x0000e800)
			return 0;
		instr = (thumbh << 16) | thumbl;

#ifdef CONFIG_NEON
		if ((instr & 0xef000000) == 0xef000000
		    || (instr & 0xff100000) == 0xf9000000)
			goto trap_vfp;
#endif
	} else {
		instr = *(unsigned *) pc;

#ifdef CONFIG_NEON
		if ((instr & 0xfe000000) == 0xf2000000
		    || (instr & 0xff100000) == 0xf4000000)
			goto trap_vfp;
#endif
	}

	if ((instr & 0x0c000000) != 0x0c000000)
		return 0;

	cp = (instr & 0x00000f00) >> 8;
#ifdef CONFIG_IWMMXT
	/* We need something equivalent to _TIF_USING_IWMMXT for Xenomai kernel
	   threads */
	if (cp <= 1) {
		d->exception = IPIPE_TRAP_FPU;
		return 1;
	}
#endif

	exc = copro_to_exc[cp];
#ifdef CONFIG_VFP
	if (exc == IPIPE_TRAP_VFP) {
	  trap_vfp:
		/* If an exception is pending, the VFP fault is not really an
		   "FPU unavailable" fault, so we return undefinstr in that
		   case, the nucleus will let linux handle the fault. */
		exc = do_vfp_fmrx(FPEXC);
		if (exc & (FPEXC_EX|FPEXC_DEX)
		    || ((exc & FPEXC_EN) && do_vfp_fmrx(FPSCR) & FPSCR_IXE))
			exc = IPIPE_TRAP_UNDEFINSTR;
		else
			exc = IPIPE_TRAP_VFP;
	}
#endif
	d->exception = exc;
	return exc != IPIPE_TRAP_UNDEFINSTR;
}

void xnarch_leave_root(struct xnthread *root)
{
	struct xnarchtcb *rootcb = xnthread_archtcb(root);
#ifdef CONFIG_VFP
	rootcb->fpup = get_fpu_owner();
#else /* !CONFIG_VFP */
	rootcb->core.user_fpu_owner = get_fpu_owner(rootcb->core.host_task);
	/* So that xnarch_save_fpu() will operate on the right FPU area. */
	rootcb->fpup = (rootcb->core.user_fpu_owner
			? task_fpenv(rootcb->core.user_fpu_owner) : NULL);
#endif /* !CONFIG_VFP */
}

#endif /* CONFIG_XENO_HW_FPU */

void xnarch_switch_to(struct xnthread *out, struct xnthread *in)
{
	struct xnarchtcb *out_tcb = &out->tcb, *in_tcb = &in->tcb;
	struct mm_struct *prev_mm, *next_mm;
	struct task_struct *next;

	next = in_tcb->core.host_task;
	prev_mm = out_tcb->core.active_mm;

	next_mm = in_tcb->core.mm;
	if (next_mm == NULL) {
		in_tcb->core.active_mm = prev_mm;
		enter_lazy_tlb(prev_mm, next);
	} else {
		ipipe_switch_mm_head(prev_mm, next_mm, next);
		/*
		 * We might be switching back to the root thread,
		 * which we preempted earlier, shortly after "current"
		 * dropped its mm context in the do_exit() path
		 * (next->mm == NULL). In that particular case, the
		 * kernel expects a lazy TLB state for leaving the mm.
		 */
		if (next->mm == NULL)
			enter_lazy_tlb(prev_mm, next);
	}

	__asm_thread_switch(out_tcb->core.tip, in_tcb->core.tip);
}

void xnarch_enable_fpu(struct xnthread *thread)
{
	struct xnarchtcb *tcb = &thread->tcb;
#ifdef CONFIG_XENO_HW_FPU
#ifdef CONFIG_VFP
	/* If we are restoring the Linux current thread which does not own the
	   FPU context, we keep FPU disabled, so that a fault will occur if the
	   newly switched thread uses the FPU, to allow the kernel handler to
	   pick the correct FPU context.
	*/
	if (likely(!tcb->is_root)) {
		do_enable_fpu();
		/* No exception should be pending, since it should have caused
		   a trap earlier.
		*/
	} else if (tcb->fpup && tcb->fpup == task_fpenv(tcb->core.host_task)) {
		unsigned fpexc = do_enable_fpu();
#ifndef CONFIG_SMP
		if (likely(!(fpexc & XNARCH_VFP_ANY_EXC)
			   && !(do_vfp_fmrx(FPSCR) & FPSCR_IXE)))
			return;
		/*
		  If current process has pending exceptions it is
		  illegal to restore the FPEXC register with them, we must
		  save the fpu state and disable them, to get linux
		  fpu fault handler take care of them correctly.
		*/
#endif
		/*
		  On SMP systems, if we are restoring the root
		  thread, running the task holding the FPU context at
		  the time when we switched to real-time domain,
		  forcibly save the FPU context. It seems to fix SMP
		  systems for still unknown reasons.
		*/
		do_save_fpu(tcb->fpup, fpexc);
		vfp_current_hw_state[ipipe_processor_id()] = NULL;
		do_disable_fpu();
	}
#else /* !CONFIG_VFP */
	if (!tcb->core.host_task)
		do_enable_fpu();
#endif /* !CONFIG_VFP */
#endif /* CONFIG_XENO_HW_FPU */
}

void xnarch_save_fpu(struct xnthread *thread)
{
	struct xnarchtcb *tcb = &thread->tcb;
#ifdef CONFIG_XENO_HW_FPU
#ifdef CONFIG_VFP
	if (tcb->fpup)
		do_save_fpu(tcb->fpup, do_enable_fpu());
#else /* !CONFIG_VFP */
	if (tcb->fpup) {
		do_save_fpu(tcb->fpup);

		if (tcb->core.user_fpu_owner && task_thread_info(tcb->core.user_fpu_owner)) {
			task_thread_info(tcb->core.user_fpu_owner)->used_cp[1] = 0;
			task_thread_info(tcb->core.user_fpu_owner)->used_cp[2] = 0;
		}
	}
#endif /* !CONFIG_VFP */
#endif /* CONFIG_XENO_HW_FPU */
}

void xnarch_restore_fpu(struct xnthread *thread)
{
	struct xnarchtcb *tcb = &thread->tcb;
#ifdef CONFIG_XENO_HW_FPU
#ifdef CONFIG_VFP
	if (likely(!tcb->is_root)) {
		do_enable_fpu();
		do_restore_fpu(tcb->fpup);
	} else {
		/*
		 * We are restoring the Linux current thread which
		 * does not own the FPU context, so the FPU must be
		 * disabled, so that a fault will occur if the newly
		 * switched thread uses the FPU, to allow the kernel
		 * handler to pick the correct FPU context.
		 *
		 * Further set vfp_current_hw_state to NULL to avoid
		 * the Linux kernel to save, when the fault occur, the
		 * current FPU context, the one of an RT task, into
		 * the FPU area of the last non RT task which used the
		 * FPU before the preemption by Xenomai.
		*/
		vfp_current_hw_state[ipipe_processor_id()] = NULL;
		do_disable_fpu();
	}
#else /* !CONFIG_VFP */
	if (tcb->fpup) {
		do_restore_fpu(tcb->fpup);

		if (tcb->core.user_fpu_owner && task_thread_info(tcb->core.user_fpu_owner)) {
			task_thread_info(tcb->core.user_fpu_owner)->used_cp[1] = 1;
			task_thread_info(tcb->core.user_fpu_owner)->used_cp[2] = 1;
		}
	}

	/* FIXME: We restore FPU "as it was" when Xenomai preempted Linux,
	   whereas we could be much lazier. */
	if (tcb->core.host_task)
		do_disable_fpu();
#endif /* !CONFIG_VFP */
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

/*
 * Copyright (C) 2001-2013 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) 2004-2006 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
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
#include <cobalt/kernel/thread.h>
#include <asm/mmu_context.h>
#include <asm/i387.h>
#include <asm/fpu-internal.h>

#ifdef CONFIG_X86_32

#ifdef CONFIG_CC_STACKPROTECTOR

#define __CANARY_OUTPUT							\
	, [stack_canary] "=m" (stack_canary.canary)

#define __CANARY_INPUT							\
	, [task_canary] "i" (offsetof(struct task_struct, stack_canary))

#define __CANARY_SWITCH							\
	"movl %P[task_canary](%%edx), %%ebx\n\t"			\
	"movl %%ebx, "__percpu_arg([stack_canary])"\n\t"

#else /* !CONFIG_CC_STACKPROTECTOR */

#define __CANARY_OUTPUT
#define __CANARY_INPUT
#define __CANARY_SWITCH

#endif /* !CONFIG_CC_STACKPROTECTOR */

static inline void do_switch_threads(struct xnarchtcb *out_tcb,
				     struct xnarchtcb *in_tcb,
				     struct task_struct *outproc,
				     struct task_struct *inproc)
{
	long ebx_out, ecx_out, edi_out, esi_out;

	__asm__ __volatile__("pushfl\n\t"
			     "pushl %%ebp\n\t"
			     "movl %[spp_out_ptr],%%ecx\n\t"
			     "movl %%esp,(%%ecx)\n\t"
			     "movl %[ipp_out_ptr],%%ecx\n\t"
			     "movl $1f,(%%ecx)\n\t"
			     "movl %[spp_in_ptr],%%ecx\n\t"
			     "movl %[ipp_in_ptr],%%edi\n\t"
			     "movl (%%ecx),%%esp\n\t"
			     "pushl (%%edi)\n\t"
			     __CANARY_SWITCH
			     "jmp  __switch_to\n\t"
			     "1: popl %%ebp\n\t"
			     "popfl\n\t"
			     : "=b"(ebx_out),
			       "=&c"(ecx_out),
			       "=S"(esi_out),
			       "=D"(edi_out),
			       "+a"(outproc),
			       "+d"(inproc)
			       __CANARY_OUTPUT
			     : [spp_out_ptr] "m"(out_tcb->spp),
			       [ipp_out_ptr] "m"(out_tcb->ipp),
			       [spp_in_ptr] "m"(in_tcb->spp),
			       [ipp_in_ptr] "m"(in_tcb->ipp)
			       __CANARY_INPUT
			     : "memory");
}

#else /* CONFIG_X86_64 */

#define __SWITCH_CLOBBER_LIST  , "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"

#ifdef CONFIG_CC_STACKPROTECTOR

#define __CANARY_OUTPUT							\
	, [gs_canary] "=m" (irq_stack_union.stack_canary)

#define __CANARY_INPUT							\
	, [task_canary] "i" (offsetof(struct task_struct, stack_canary)) \
	, [current_task] "m" (current_task)

#define __CANARY_SWITCH							\
  	"movq "__percpu_arg([current_task])",%%rsi\n\t"			\
	"movq %P[task_canary](%%rsi),%%r8\n\t"				\
	"movq %%r8,"__percpu_arg([gs_canary])"\n\t"

#else /* !CONFIG_CC_STACKPROTECTOR */

#define __CANARY_OUTPUT
#define __CANARY_INPUT
#define __CANARY_SWITCH

#endif /* !CONFIG_CC_STACKPROTECTOR */

#define do_switch_threads(prev, next, p_rsp, n_rsp, p_rip, n_rip)	\
	({								\
		long __rdi, __rsi, __rax, __rbx, __rcx, __rdx;		\
									\
		__asm__ __volatile__("pushfq\n\t"			\
			     "pushq	%%rbp\n\t"			\
			     "movq	%%rsi, %%rbp\n\t"		\
			     "movq	%%rsp, (%%rdx)\n\t"		\
			     "movq	$1f, (%%rax)\n\t"		\
			     "movq	(%%rcx), %%rsp\n\t"		\
			     "pushq	(%%rbx)\n\t"			\
			     "jmp	__switch_to\n\t"		\
			     "1:\n\t"					\
			     __CANARY_SWITCH				\
			     "movq	%%rbp, %%rsi\n\t"		\
			     "popq	%%rbp\n\t"			\
			     "popfq\n\t"				\
			     : "=S" (__rsi), "=D" (__rdi), "=a"	(__rax), \
			       "=b" (__rbx), "=c" (__rcx), "=d" (__rdx)	\
			       __CANARY_OUTPUT				\
			     : "0" (next), "1" (prev), "5" (p_rsp), "4" (n_rsp), \
			       "2" (p_rip), "3" (n_rip)			\
			       __CANARY_INPUT				\
			     : "memory", "cc" __SWITCH_CLOBBER_LIST);	\
	})

#endif /* CONFIG_X86_64 */

void xnarch_switch_to(struct xnthread *out, struct xnthread *in)
{
	struct xnarchtcb *out_tcb = &out->tcb, *in_tcb = &in->tcb;
	unsigned long __maybe_unused fs, gs;
	struct mm_struct *prev_mm, *next_mm;
	struct task_struct *prev, *next;

	prev = out_tcb->core.host_task;
	if (__thread_has_fpu(prev))
		/*
		 * __switch_to will try and use __unlazy_fpu, so we
		 * need to clear the ts bit.
		 */
		clts();

	next = in_tcb->core.host_task;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,13,0)
	next->thread.fpu_counter = 0;
#else
	next->fpu_counter = 0;
#endif

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

#ifdef CONFIG_X86_32
	/*
	 * Make sure that __switch_to() will always reload the correct
	 * %fs and %gs registers, even if we happen to migrate the
	 * task across domains in the meantime.
	 */
	asm volatile ("mov %%fs,%0":"=m" (fs));
	asm volatile ("mov %%gs,%0":"=m" (gs));

	do_switch_threads(out_tcb, in_tcb, prev, next);

	if (xnarch_shadow_p(out_tcb, prev)) {
		loadsegment(fs, fs);
		loadsegment(gs, gs);
		barrier();
	}
#else /* CONFIG_X86_64 */
	do_switch_threads(prev, next,
			  out_tcb->spp, in_tcb->spp,
			  out_tcb->ipp, in_tcb->ipp);
#endif /* CONFIG_X86_64 */

	stts();
}

#ifdef CONFIG_X86_64
#define XSAVE_PREFIX	"0x48,"
#define XSAVE_SUFFIX	"q"
#else
#define XSAVE_PREFIX
#define XSAVE_SUFFIX
#endif

static inline void __do_save_i387(x86_fpustate *fpup)
{
#ifdef cpu_has_xsave
	if (cpu_has_xsave) {
#ifdef CONFIG_AS_AVX
		__asm__ __volatile__("xsave" XSAVE_SUFFIX " %0"
			     : "=m" (fpup->xsave) : "a" (-1), "d" (-1)
			     : "memory");
#else /* !CONFIG_AS_AVX */
		__asm __volatile__(".byte " XSAVE_PREFIX "0x0f,0xae,0x27"
			     : : "D" (&fpup->xsave), "m" (fpup->xsave),
			         "a" (-1), "d" (-1)
			     : "memory");
#endif /* !CONFIG_AS_AVX */
		return;
	}
#endif /* cpu_has_xsave */
#ifdef CONFIG_X86_32
	if (cpu_has_fxsr)
		__asm__ __volatile__("fxsave %0; fnclex":"=m"(*fpup));
	else
		__asm__ __volatile__("fnsave %0; fwait":"=m"(*fpup));
#else /* CONFIG_X86_64 */
#ifdef CONFIG_AS_FXSAVEQ
	__asm __volatile__("fxsaveq %0" : "=m" (fpup->fxsave));
#else /* !CONFIG_AS_FXSAVEQ */
	__asm__ __volatile__("rex64/fxsave (%[fx])"
		     : "=m" (fpup->fxsave)
		     : [fx] "R" (&fpup->fxsave));
#endif /* !CONFIG_AS_FXSAVEQ */
#endif /* CONFIG_X86_64 */
}

static inline void __do_restore_i387(x86_fpustate *fpup)
{
#ifdef cpu_has_xsave
	if (cpu_has_xsave) {
#ifdef CONFIG_AS_AVX
		__asm__ __volatile__("xrstor" XSAVE_SUFFIX " %0"
			     : : "m" (fpup->xsave), "a" (-1), "d" (-1)
			     : "memory");
#else /* !CONFIG_AS_AVX */
		__asm__ __volatile__(".byte " XSAVE_PREFIX "0x0f,0xae,0x2f"
			     : : "D" (&fpup->xsave), "m" (fpup->xsave),
			         "a" (-1), "d" (-1)
			     : "memory");
#endif /* !CONFIG_AS_AVX */
		return;
	}
#endif /* cpu_has_xsave */
#ifdef CONFIG_X86_32
	if (cpu_has_fxsr)
		__asm__ __volatile__("fxrstor %0": /* no output */ :"m"(*fpup));
	else
		__asm__ __volatile__("frstor %0": /* no output */ :"m"(*fpup));
#else /* CONFIG_X86_64 */
#ifdef CONFIG_AS_FXSAVEQ
	__asm__ __volatile__("fxrstorq %0" : : "m" (fpup->fxsave));
#else /* !CONFIG_AS_FXSAVEQ */
	__asm__ __volatile__("rex64/fxrstor (%0)"
		     : : "R" (&fpup->fxsave), "m" (fpup->fxsave));
#endif /* !CONFIG_AS_FXSAVEQ */
#endif /* CONFIG_X86_64 */
}

int xnarch_handle_fpu_fault(struct xnthread *from, 
			struct xnthread *to, struct ipipe_trap_data *d)
{
	struct xnarchtcb *tcb = xnthread_archtcb(to);
	struct task_struct *p = tcb->core.host_task;

	if (__thread_has_fpu(p))
		return 0;

	if (tsk_used_math(p) == 0) {
		/*
		 * The faulting task is a shadow using the FPU for the first
		 * time, initialize the FPU context and tell linux about it.
		 * The fpu usage bit is necessary for xnarch_save_fpu() to
		 * save the FPU state at next switch.
		 */
		__asm__ __volatile__("clts; fninit");

		if (cpu_has_xmm) {
			unsigned long __mxcsr = 0x1f80UL & 0xffbfUL;
			__asm__ __volatile__("ldmxcsr %0"::"m"(__mxcsr));
		}
		set_stopped_child_used_math(p);
	} else {
		/*
		 * The faulting task already used FPU in secondary
		 * mode.
		 */
		clts();
		__do_restore_i387(tcb->fpup);
	}
		
	__thread_set_has_fpu(p);

	xnlock_get(&nklock);
	xnthread_set_state(to, XNFPU);
	xnlock_put(&nklock);

	return 1;
}

#define current_task_used_kfpu() kernel_fpu_disabled()

#define tcb_used_kfpu(t) ((t)->root_kfpu)

void xnarch_leave_root(struct xnthread *root)
{
	struct xnarchtcb *const rootcb = xnthread_archtcb(root);
	struct task_struct *const p = current;
	x86_fpustate *const current_task_fpup = x86_fpustate_ptr(&p->thread);

#ifdef CONFIG_X86_64
	rootcb->spp = &p->thread.sp;
	rootcb->ipp = &p->thread.rip;
#endif
	if (current_task_used_kfpu() == 0) {
		rootcb->root_kfpu = 0;
		rootcb->fpup = __thread_has_fpu(p) ? current_task_fpup : NULL;
		return;
	}

	rootcb->root_kfpu = 1;
	rootcb->fpup = current_task_fpup;
	rootcb->root_used_math = tsk_used_math(p) != 0;
	x86_fpustate_ptr(&p->thread) = &rootcb->i387;
	__thread_set_has_fpu(p);
	set_stopped_child_used_math(p);
	kernel_fpu_enable();
}

void xnarch_save_fpu(struct xnthread *thread)
{
	struct xnarchtcb *tcb = xnthread_archtcb(thread);
	struct task_struct *p = tcb->core.host_task;

	if (__thread_has_fpu(p) == 0)
		/* Saved by last __switch_to */
		return;
	
	clts();

	__do_save_i387(x86_fpustate_ptr(&p->thread));
	__thread_clear_has_fpu(p);
}

void xnarch_switch_fpu(struct xnthread *from, struct xnthread *to)
{
	x86_fpustate *const from_fpup = from ? from->tcb.fpup : NULL;
	struct xnarchtcb *const tcb = xnthread_archtcb(to);
	struct task_struct *const p = tcb->core.host_task;
	x86_fpustate *const current_task_fpup = x86_fpustate_ptr(&p->thread);

	if (xnthread_test_state(to, XNROOT) && from_fpup != current_task_fpup &&
		tcb_used_kfpu(tcb) == 0)
		/* Only restore lazy mode if root fpu owner is not current */
		return;

	clts();
	/*
	 * The only case where we can skip restoring the FPU is:
	 * - the fpu context of the current task is the current fpu
	 * context;
	 * - root thread has not used fpu in kernel-space;
	 * - cpu has fxsr (because if it does not, last context switch
	 * reinitialized fpu)
	 */
	if (from_fpup != current_task_fpup || cpu_has_fxsr == 0)
		__do_restore_i387(current_task_fpup);
	if (tcb_used_kfpu(tcb) == 0) {
		__thread_set_has_fpu(p);
		return;
	}
	kernel_fpu_disable();

	x86_fpustate_ptr(&p->thread) = to->tcb.fpup;
	if (tcb->root_used_math == 0) {
		__thread_clear_has_fpu(p);
		clear_stopped_child_used_math(p);
	}
}

void xnarch_init_root_tcb(struct xnthread *thread)
{
	struct xnarchtcb *tcb = xnthread_archtcb(thread);
	tcb->sp = 0;
	tcb->spp = &tcb->sp;
	tcb->ipp = &tcb->ip;
	tcb->fpup = NULL;
	tcb->root_kfpu = 0;
}

void xnarch_init_shadow_tcb(struct xnthread *thread)
{
	struct xnarchtcb *tcb = xnthread_archtcb(thread);
	struct task_struct *p = tcb->core.host_task;

	tcb->sp = 0;
	tcb->spp = &p->thread.sp;
#ifdef CONFIG_X86_32
	tcb->ipp = &p->thread.ip;
#else
	tcb->ipp = &p->thread.rip; /* <!> raw naming intended. */
#endif
	tcb->fpup = x86_fpustate_ptr(&p->thread);
	tcb->root_kfpu = 0;

	/* XNFPU is set upon first FPU fault */
	xnthread_clear_state(thread, XNFPU);
}

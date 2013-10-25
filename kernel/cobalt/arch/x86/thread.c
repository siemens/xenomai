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
#include <asm/mmu_context.h>
#include <cobalt/kernel/thread.h>

#ifdef CONFIG_X86_32

static inline void do_switch_threads(struct xnarchtcb *out_tcb,
				     struct xnarchtcb *in_tcb,
				     struct task_struct *outproc,
				     struct task_struct *inproc)
{
	long ebx_out, ecx_out, edi_out, esi_out;

	__asm__ __volatile__("pushfl\n\t"
			     "pushl %%ebp\n\t"
			     "movl %6,%%ecx\n\t"
			     "movl %%esp,(%%ecx)\n\t"
			     "movl %7,%%ecx\n\t"
			     "movl $1f,(%%ecx)\n\t"
			     "movl %8,%%ecx\n\t"
			     "movl %9,%%edi\n\t"
			     "movl (%%ecx),%%esp\n\t"
			     "pushl (%%edi)\n\t"
			     "jmp  __switch_to\n\t"
			     "1: popl %%ebp\n\t"
			     "popfl\n\t":"=b"(ebx_out),
			     "=&c"(ecx_out),
			     "=S"(esi_out),
			     "=D"(edi_out), "+a"(outproc), "+d"(inproc)
			     :"m"(out_tcb->spp),
			      "m"(out_tcb->ipp),
			      "m"(in_tcb->spp), "m"(in_tcb->ipp));
}

#else /* CONFIG_X86_64 */

#define __SWITCH_CLOBBER_LIST  , "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"

#define do_switch_threads(prev,next,p_rsp,n_rsp,p_rip,n_rip)		\
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
			     "movq	%%rbp, %%rsi\n\t"		\
			     "popq	%%rbp\n\t"			\
			     "popfq\n\t"				\
			     : "=S" (__rsi), "=D" (__rdi), "=a"	(__rax), \
			       "=b" (__rbx), "=c" (__rcx), "=d" (__rdx)	\
			     : "0" (next), "1" (prev), "5" (p_rsp), "4" (n_rsp), \
			       "2" (p_rip), "3" (n_rip)			\
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
	if (wrap_test_fpu_used(prev))
		/*
		 * __switch_to will try and use __unlazy_fpu, so we
		 * need to clear the ts bit.
		 */
		clts();

	next = in_tcb->core.host_task;
	next->fpu_counter = 0;

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

#ifdef CONFIG_XENO_HW_FPU

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

	if (tsk_used_math(p))
		return 0;

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
	wrap_set_fpu_used(p);

	return 1;
}

void xnarch_leave_root(struct xnthread *root)
{
	struct xnarchtcb *rootcb = xnthread_archtcb(root);
	struct task_struct *p = current;

#ifdef CONFIG_X86_64
	rootcb->spp = &p->thread.sp;
	rootcb->ipp = &p->thread.rip;
#endif
	rootcb->fpup = x86_fpustate_ptr(&p->thread);
	rootcb->root_kfpu = 
		(read_cr0() & 8) == 0 && wrap_test_fpu_used(p) == 0;
	if (rootcb->root_kfpu) {
		rootcb->root_used_math = tsk_used_math(p) != 0;
		x86_fpustate_ptr(&p->thread) = &rootcb->i387;
		wrap_set_fpu_used(p);
		set_stopped_child_used_math(p);
	}
}

void xnarch_save_fpu(struct xnthread *thread)
{
	struct xnarchtcb *tcb = xnthread_archtcb(thread);
	struct task_struct *p = tcb->core.host_task;

	if (wrap_test_fpu_used(p) == 0)
		/* Common case: already saved by __switch_to */
		return;
	
	/* Exceptional case: a migrating thread */
	clts();

	__do_save_i387(x86_fpustate_ptr(&p->thread));
	wrap_clear_fpu_used(p);
}

void xnarch_switch_fpu(struct xnthread *from, struct xnthread *thread)
{
	struct xnarchtcb *tcb = xnthread_archtcb(thread);
	struct task_struct *p = tcb->core.host_task;

	if (tcb->root_kfpu == 0 && 
		(tsk_used_math(p) == 0 || xnthread_test_state(thread, XNROOT)))
		/* Restore lazy mode */
		return;

	/*
	 * Restore the FPU hardware with valid fp registers from a
	 * RT user-space or kernel thread.
	 */
	clts();

	__do_restore_i387(x86_fpustate_ptr(&p->thread));
	if (tcb->root_kfpu) {
		x86_fpustate_ptr(&p->thread) = tcb->fpup;
		wrap_clear_fpu_used(p);
		if (tcb->root_used_math == 0)
			clear_stopped_child_used_math(p);
	} else
		wrap_set_fpu_used(p);
}

#endif /* CONFIG_XENO_HW_FPU */

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
}

int xnarch_escalate(void)
{
	if (ipipe_root_p) {
		ipipe_raise_irq(xnarch_machdata.escalate_virq);
		return 1;
	}

	return 0;
}

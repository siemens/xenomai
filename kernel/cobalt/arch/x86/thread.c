/*
 * Copyright (C) 2001-2007 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) 2004-2006 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
 * Copyright (C) 2004 The HYADES Project (http://www.hyades-itea.org).
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
#include <nucleus/clock.h>

asmlinkage static void thread_trampoline(struct xnarchtcb *tcb);

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
			     "movl $2f,(%%ecx)\n\t"
			     "movl %8,%%ecx\n\t"
			     "movl %9,%%edi\n\t"
			     "movl (%%ecx),%%esp\n\t"
			     "pushl (%%edi)\n\t"
			     "testl %%edx,%%edx\n\t"
			     "je 1f\n\t"
			     "cmp %%edx,%%eax\n\t"
			     "jne  __switch_to\n\t"
			     "1: ret\n\t"
			     "2: popl %%ebp\n\t"
			     "popfl\n\t":"=b"(ebx_out),
			     "=&c"(ecx_out),
			     "=S"(esi_out),
			     "=D"(edi_out), "+a"(outproc), "+d"(inproc)
			     :"m"(out_tcb->spp),
			      "m"(out_tcb->ipp),
			      "m"(in_tcb->spp), "m"(in_tcb->ipp));
}

void xnarch_init_thread(struct xnarchtcb *tcb,
			void (*entry) (void *),	void *cookie,
			int imask, struct xnthread *thread, char *name)
{
	unsigned long **psp = (unsigned long **)&tcb->sp;

	tcb->ip = (unsigned long)thread_trampoline;
	tcb->sp = (unsigned long)tcb->stackbase;
	*psp = (unsigned long *)
		(((unsigned long)*psp + tcb->stacksize - 0x10) & ~0xf);
	*--(*psp) = (unsigned long)tcb;
	*--(*psp) = 0;
}

#define thread_prologue		do { } while (0)

#else /* CONFIG_X86_64 */

struct xnarch_x8664_initstack {
#ifdef CONFIG_CC_STACKPROTECTOR
	unsigned long canary;
#endif
	unsigned long rbp;
	unsigned long eflags;
	unsigned long arg;
	unsigned long entry;
};

#ifdef CONFIG_CC_STACKPROTECTOR
/*
 * We have an added complexity with -fstack-protector, due to the
 * hybrid scheduling between user- and kernel-based Xenomai threads,
 * for which we do not have any underlying task_struct.  We update the
 * current stack canary for incoming user-based threads exactly the
 * same way as Linux does. However, we handle the case of incoming
 * kernel-based Xenomai threads differently: in that case, the canary
 * value shall be given by our caller.
 *
 * In the latter case, the update logic is jointly handled by
 * switch_kernel_prologue and switch_kernel_canary; the former clears
 * %rax whenever the incoming thread is kernel-based, and with that
 * information, the latter checks %rax to determine whether the canary
 * should be picked from the current task struct, or from %r8. %r8 is
 * set ealier to the proper canary value passed by our caller
 * (i.e. in_tcb->canary).
 *
 * This code is based on the assumption that no scheduler exchange can
 * happen between Linux and Xenomai for kernel-based Xenomai threads,
 * i.e. the only way to schedule a kernel-based Xenomai thread in goes
 * through xnarch_switch_to(), never via schedule(). This is turn
 * means that neither %rax or %r8 could be clobbered once set by
 * switch_kernel_prologue, since the only way of branching to the
 * incoming kernel-based thread is via retq. Besides, we may take for
 * granted that %rax can't be null upon return from __switch_to, since
 * the latter returns prev_p, which cannot be NULL, so the logic
 * cannot be confused by incoming user-based threads.
 *
 * Yeah, I know, it's awfully convoluted, but I'm not even sorry for
 * this. --rpm
 */
#define init_kernel_canary						\
	"popq	%%r8\n\t"						\
	"movq %%r8,"__percpu_arg([gs_canary])"\n\t"
#define init_canary_oparam						\
	[gs_canary] "=m" (irq_stack_union.stack_canary)
#define switch_canary_setup(c)				\
	register long __kcanary __asm__ ("r8") = (c)
#define switch_kernel_prologue			\
	"xor %%rax, %%rax\n\t"
#define switch_kernel_canary						\
	"testq %%rax, %%rax\n\t"					\
	"jnz 8f\n\t"							\
	"movq %%r8,"__percpu_arg([gs_canary])"\n\t"			\
	"jmp 9f\n\t"
#define switch_user_canary						\
	"movq "__percpu_arg([current_task])",%%rsi\n\t"			\
	"movq %P[user_canary](%%rsi),%%r8\n\t"				\
	"movq %%r8,"__percpu_arg([gs_canary])"\n\t"
#define switch_canary_oparam					\
	, [gs_canary] "=m" (irq_stack_union.stack_canary)
#define switch_canary_iparam					\
	, [user_canary] "i" (offsetof(struct task_struct, stack_canary)) \
	, [current_task] "m" (current_task)				\
	, "r" (__kcanary)
#define switch_canary(in)  (in)->canary
#define __SWITCH_CLOBBER_LIST  , "r9", "r10", "r11", "r12", "r13", "r14", "r15"
#define __HEAD_CLOBBER_LIST    , "rdi", "r8"
#else	/* CC_STACKPROTECTOR */
#define switch_canary_setup(c)	(void)(c)
#define init_kernel_canary
#define init_canary_oparam
#define switch_kernel_prologue
#define switch_kernel_canary
#define switch_user_canary
#define switch_canary_oparam
#define switch_canary_iparam
#define switch_canary(in)  0
#define __SWITCH_CLOBBER_LIST  , "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
#define __HEAD_CLOBBER_LIST    , "rdi"
#endif	/* CC_STACKPROTECTOR */

#define do_switch_threads(prev,next,p_rsp,n_rsp,p_rip,n_rip,kcanary)	\
	({								\
		long __rdi, __rsi, __rax, __rbx, __rcx, __rdx;		\
		switch_canary_setup(kcanary);				\
									\
		asm volatile("pushfq\n\t"				\
			     "pushq	%%rbp\n\t"			\
			     "movq	%%rsi, %%rbp\n\t"		\
			     "movq	%%rsp, (%%rdx)\n\t"		\
			     "movq	$1f, (%%rax)\n\t"		\
			     "movq	(%%rcx), %%rsp\n\t"		\
			     "pushq	(%%rbx)\n\t"			\
			     "cmpq	%%rsi, %%rdi\n\t"		\
			     "jz	0f\n\t"				\
			     "testq	%%rsi, %%rsi\n\t"		\
			     "jnz	__switch_to\n\t"		\
			     switch_kernel_prologue			\
			     "0:\n\t"					\
			     "ret\n\t"					\
			     "1:\n\t"					\
			     switch_kernel_canary			\
			     "8:\n\t"					\
			     switch_user_canary				\
			     "9:\n\t"					\
			     "movq	%%rbp, %%rsi\n\t"		\
			     "popq	%%rbp\n\t"			\
			     "popfq\n\t"				\
			     : "=S" (__rsi), "=D" (__rdi), "=a"	(__rax), \
			       "=b" (__rbx), "=c" (__rcx), "=d" (__rdx)	\
			       switch_canary_oparam		\
			     : "0" (next), "1" (prev), "5" (p_rsp), "4" (n_rsp), \
			       "2" (p_rip), "3" (n_rip)			\
			       switch_canary_iparam		\
			     : "memory", "cc" __SWITCH_CLOBBER_LIST);	\
	})

#define thread_prologue							\
asm volatile(".globl __thread_prologue\n\t"				\
	       "__thread_prologue:\n\t"					\
	       init_kernel_canary					\
	       "popq	%%rbp\n\t"					\
	       "popfq\n\t"						\
	       "popq	%%rdi\n\t"					\
	       "ret\n\t"						\
	       : init_canary_oparam					\
	       : /* no input */						\
	       : "cc", "memory" __HEAD_CLOBBER_LIST)

asmlinkage void __thread_prologue(void);

void xnarch_init_thread(struct xnarchtcb *tcb,
			void (*entry)(void *), void *cookie,
			int imask, struct xnthread *thread, char *name)
{
	struct xnarch_x8664_initstack *childregs;
	unsigned long *rsp, flags;

	/* Prepare the bootstrap stack. */

	flags = hard_local_save_flags();

	rsp = (unsigned long *)((unsigned long)tcb->stackbase + tcb->stacksize -
				sizeof(struct xnarch_x8664_initstack) - 8);

	childregs = (struct xnarch_x8664_initstack *)rsp;
	childregs->rbp = 0;
	childregs->eflags = flags & ~X86_EFLAGS_IF;
	childregs->arg = (unsigned long)tcb;
	childregs->entry = (unsigned long)thread_trampoline;
#ifdef CONFIG_CC_STACKPROTECTOR
	tcb->canary = (unsigned long)xnclock_read_raw() ^ childregs->arg;
	childregs->canary = tcb->canary;
#endif
	tcb->sp = (unsigned long)childregs;
	tcb->ip = (unsigned long)__thread_prologue; /* Will branch there at startup. */
	tcb->entry = entry;
	tcb->cookie = cookie;
	tcb->self = thread;
	tcb->imask = imask;
	tcb->name = name;
}

#endif /* CONFIG_X86_64 */

asmlinkage static void thread_trampoline(struct xnarchtcb *tcb)
{
	/* xnpod_welcome_thread() will do clts() if needed. */
	stts();
	xnpod_welcome_thread(tcb->self, tcb->imask);
	tcb->entry(tcb->cookie);
	xnpod_delete_thread(tcb->self);
	thread_prologue;
}

void xnarch_leave_root(struct xnarchtcb *rootcb)
{
	ipipe_notify_root_preemption();

	/* Remember the preempted Linux task pointer. */
	rootcb->user_task = rootcb->active_task = current;
	rootcb->ts_usedfpu = wrap_test_fpu_used(current) != 0;
	rootcb->cr0_ts = (read_cr0() & 8) != 0;
#ifdef CONFIG_X86_64
	rootcb->spp = &current->thread.sp;
	rootcb->ipp = &current->thread.rip;
#endif
	/* So that xnarch_save_fpu() will operate on the right FPU area. */
	if (rootcb->cr0_ts || rootcb->ts_usedfpu)
		rootcb->fpup = x86_fpustate_ptr(&rootcb->user_task->thread);
	else
		/*
		 * The kernel is currently using fpu in kernel-space,
		 * do not clobber the user-space fpu backup area.
		 */
		rootcb->fpup = &rootcb->i387;
}

void xnarch_switch_to(struct xnarchtcb *out_tcb, struct xnarchtcb *in_tcb)
{
	struct task_struct *prev = out_tcb->active_task;
	struct task_struct *next = in_tcb->user_task;
	unsigned long __maybe_unused fs, gs;
	struct mm_struct *oldmm;

	if (likely(next)) {
		if (wrap_test_fpu_used(prev))
			/*
			 * __switch_to will try and use __unlazy_fpu,
			 * so we need to clear the ts bit.
			 */
			clts();
		in_tcb->active_task = next;
		ipipe_clear_foreign_stack(&xnarch_machdata.domain);
		next->fpu_counter = 0;
	} else {
		in_tcb->active_task = prev;
		ipipe_set_foreign_stack(&xnarch_machdata.domain);
	}

	if (next && next != prev) {
		oldmm = prev->active_mm;
		switch_mm(oldmm, next->active_mm, next);
		if (next->mm == NULL)
			enter_lazy_tlb(oldmm, next);
	}

#ifdef CONFIG_X86_32
	if (out_tcb->user_task) {
		/* Make sure that __switch_to() will always reload the correct
		   %fs and %gs registers, even if we happen to migrate the task
		   across domains in the meantime. */
		asm volatile ("mov %%fs,%0":"=m" (fs));
		asm volatile ("mov %%gs,%0":"=m" (gs));
	}

	do_switch_threads(out_tcb, in_tcb, prev, next);

	if (xnarch_shadow_p(out_tcb, prev)) {
		loadsegment(fs, fs);
		loadsegment(gs, gs);
		barrier();
	}
#else /* CONFIG_X86_64 */
	do_switch_threads(prev, next,
			  out_tcb->spp, in_tcb->spp,
			  out_tcb->ipp, in_tcb->ipp,
			  switch_canary(in_tcb));
#endif /* CONFIG_X86_64 */

	stts();
}

#ifdef CONFIG_XENO_HW_FPU

static inline int __do_save_i387(x86_fpustate *fpup)
{
	int err = 0;

#ifdef CONFIG_X86_32
	if (cpu_has_fxsr)
		__asm__ __volatile__("fxsave %0; fnclex":"=m"(*fpup));
	else
		__asm__ __volatile__("fnsave %0; fwait":"=m"(*fpup));
#else /* CONFIG_X86_64 */
	struct i387_fxsave_struct *fx = &fpup->fxsave;

	asm volatile("1:  rex64/fxsave (%[fx])\n\t"
		     "2:\n"
		     ".section .fixup,\"ax\"\n"
		     "3:  movl $-1,%[err]\n"
		     "    jmp  2b\n"
		     ".previous\n"
		     ".section __ex_table,\"a\"\n"
		     "   .align 8\n"
		     "   .quad  1b,3b\n"
		     ".previous"
		     : [err] "=r" (err), "=m" (*fx)
		     : [fx] "cdaSDb" (fx), "0" (0));
#endif /* CONFIG_X86_64 */

	return err;
}

static inline int __do_restore_i387(x86_fpustate *fpup)
{
	int err = 0;

#ifdef CONFIG_X86_32
	if (cpu_has_fxsr)
		__asm__ __volatile__("fxrstor %0": /* no output */ :"m"(*fpup));
	else
		__asm__ __volatile__("frstor %0": /* no output */ :"m"(*fpup));
#else /* CONFIG_X86_64 */
	struct i387_fxsave_struct *fx = &fpup->fxsave;

	asm volatile("1:  rex64/fxrstor (%[fx])\n\t"
		     "2:\n"
		     ".section .fixup,\"ax\"\n"
		     "3:  movl $-1,%[err]\n"
		     "    jmp  2b\n"
		     ".previous\n"
		     ".section __ex_table,\"a\"\n"
		     "   .align 8\n"
		     "   .quad  1b,3b\n"
		     ".previous"
		     : [err] "=r" (err)
		     : [fx] "cdaSDb" (fx), "m" (*fx), "0" (0));
#endif /* CONFIG_X86_64 */

	return err;
}

void xnarch_init_fpu(struct xnarchtcb *tcb)
{
	struct task_struct *task = tcb->user_task;

	/*
	 * Initialize the FPU for a task. This must be run on behalf
	 * of the task.
	 */
	__asm__ __volatile__("clts; fninit");

	if (cpu_has_xmm) {
		unsigned long __mxcsr = 0x1f80UL & 0xffbfUL;
		__asm__ __volatile__("ldmxcsr %0"::"m"(__mxcsr));
	}

	/*
	 * Real-time shadow FPU initialization: tell Linux that this
	 * thread initialized its FPU hardware. The fpu usage bit is
	 * necessary for xnarch_save_fpu to save the FPU state at next
	 * switch.
	 */
	if (task) {
		set_stopped_child_used_math(task);
		wrap_set_fpu_used(task);
	}
}

void xnarch_save_fpu(struct xnarchtcb *tcb)
{
	struct task_struct *task = tcb->user_task;

	if (tcb->is_root == 0) {
		if (task) {
			/* fpu not used or already saved by __switch_to. */
			if (wrap_test_fpu_used(task) == 0)
				return;

			/*
			 * Tell Linux that we already saved the state
			 * of the FPU hardware of this task.
			 */
			wrap_clear_fpu_used(task);
		}
	} else {
		if (tcb->cr0_ts ||
		    (tcb->ts_usedfpu && wrap_test_fpu_used(task) == 0))
			return;

		wrap_clear_fpu_used(task);
	}

	clts();

	__do_save_i387(tcb->fpup);
}

void xnarch_restore_fpu(struct xnarchtcb *tcb)
{
	struct task_struct *task = tcb->user_task;

	if (tcb->is_root == 0) {
		if (task) {
			if (xnarch_fpu_init_p(task) == 0) {
				stts();
				return;	/* Uninit fpu area -- do not restore. */
			}

			/* Tell Linux that this task has altered the state of
			 * the FPU hardware. */
			wrap_set_fpu_used(task);
		}
	} else {
		/* Restore state of FPU only if TS bit in cr0 was clear. */
		if (tcb->cr0_ts) {
			wrap_clear_fpu_used(task);
			stts();
			return;
		}

		if (tcb->ts_usedfpu && !wrap_test_fpu_used(task)) {
			/*
			 * __switch_to saved the fpu context, no need
			 * to restore it since we are switching to
			 * root, where fpu can be in lazy state.
			 */
			stts();
			return;
		}
	}

	/* Restore the FPU hardware with valid fp registers from a
	   user-space or kernel thread. */
	clts();

	__do_restore_i387(tcb->fpup);
}

void xnarch_enable_fpu(struct xnarchtcb *tcb)
{
	struct task_struct *task = tcb->user_task;

	if (tcb->is_root == 0) {
		if (task) {
			if (xnarch_fpu_init_p(task) == 0)
				return;
			/*
			 * We used to test here if __switch_to had not
			 * saved current fpu state, but this can not
			 * happen, since xnarch_enable_fpu may only be
			 * called when switching back to a user-space
			 * task after one or several switches to
			 * non-fpu kernel-space real-time tasks, so
			 * xnarch_switch_to never uses __switch_to.
			 */
		}
	} else if (tcb->cr0_ts)
		return;

		/* The comment in the non-root case applies here too. */

	clts();
}

#endif /* CONFIG_XENO_HW_FPU */

void xnarch_init_root_tcb(struct xnarchtcb *tcb,
			  struct xnthread *thread,
			  const char *name)
{
	tcb->user_task = current;
	tcb->active_task = NULL;
	tcb->sp = 0;
	tcb->spp = &tcb->sp;
	tcb->ipp = &tcb->ip;
	tcb->fpup = NULL;
	tcb->entry = NULL;
	tcb->cookie = NULL;
	tcb->self = thread;
	tcb->imask = 0;
	tcb->name = name;
	tcb->is_root = 1;
}

void xnarch_init_shadow_tcb(struct xnarchtcb *tcb,
			    struct xnthread *thread,
			    const char *name)
{
	struct task_struct *task = current;

	tcb->user_task = task;
	tcb->active_task = NULL;
	tcb->sp = 0;
	tcb->spp = &task->thread.sp;
#ifdef CONFIG_X86_32
	tcb->ipp = &task->thread.ip;
#else
	tcb->ipp = &task->thread.rip; /* <!> raw naming intended. */
#endif
	tcb->fpup = x86_fpustate_ptr(&task->thread);
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
	tcb->spp = &tcb->sp;
	tcb->ipp = &tcb->ip;
	tcb->mayday.ip = 0;
	tcb->fpup = &tcb->i387;
	tcb->is_root = 0;
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

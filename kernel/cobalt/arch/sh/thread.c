/*
 * Copyright (C) 2011 Philippe Gerum <rpm@xenomai.org>.
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
#include <asm/system.h>
#include <nucleus/pod.h>
#include <nucleus/heap.h>
#include <asm/xenomai/thread.h>

asmlinkage void __asm_thread_trampoline(void);

/*
 * Most of this code was lifted from the regular Linux task switching
 * code. A provision for handling Xenomai-originated kernel threads
 * (aka "hybrid scheduling" is added).
 */
#define do_switch_threads(otcb, itcb, prev, next)		\
	({							\
	register u32 *__ts1 __asm__ ("r1");			\
	register u32 *__ts2 __asm__ ("r2");			\
	register u32 *__ts4 __asm__ ("r4");			\
	register u32 *__ts5 __asm__ ("r5");			\
	register u32 *__ts6 __asm__ ("r6");			\
	register u32 __ts7 __asm__ ("r7");			\
	struct task_struct *__last = prev;			\
	struct xnarchtcb *__ltcb = otcb;			\
								\
	if (otcb->tsp == &prev->thread &&			\
	    is_dsp_enabled(prev))				\
		__save_dsp(prev);				\
								\
	__ts1 = (u32 *)&otcb->tsp->sp;				\
	__ts2 = (u32 *)&otcb->tsp->pc;				\
	__ts4 = (u32 *)prev;					\
	__ts5 = (u32 *)next;					\
	__ts6 = (u32 *)&itcb->tsp->sp;				\
	__ts7 = itcb->tsp->pc;					\
								\
	__asm__ __volatile__ (					\
		".balign 4\n\t"					\
		"stc.l	gbr, @-r15\n\t"				\
		"sts.l	pr, @-r15\n\t"				\
		"mov.l	r8, @-r15\n\t"				\
		"mov.l	r9, @-r15\n\t"				\
		"mov.l	r10, @-r15\n\t"				\
		"mov.l	r11, @-r15\n\t"				\
		"mov.l	r12, @-r15\n\t"				\
		"mov.l	r13, @-r15\n\t"				\
		"mov.l	r14, @-r15\n\t"				\
		"mov.l	r15, @r1\t! save SP\n\t"		\
		"mov.l	@r6, r15\t! change to new stack\n\t"	\
		"mova	1f, %0\n\t"				\
		"mov.l	%0, @r2\t! save PC\n\t"			\
		"mov	#0, r8\n\t"				\
		"cmp/eq r5, r8\n\t"				\
		"bt/s   3f\n\t"					\
		" lds	r7, pr\t!  return to saved PC\n\t"	\
		"mov.l	2f, %0\n\t"				\
		"jmp	@%0\t! call __switch_to\n\t"		\
		" nop\t\n\t"					\
		"3:\n\t"					\
		"rts\n\t"					\
		".balign	4\n"				\
		"2:\n\t"					\
		".long	__switch_to\n"				\
		"1:\n\t"					\
		"mov.l	@r15+, r14\n\t"				\
		"mov.l	@r15+, r13\n\t"				\
		"mov.l	@r15+, r12\n\t"				\
		"mov.l	@r15+, r11\n\t"				\
		"mov.l	@r15+, r10\n\t"				\
		"mov.l	@r15+, r9\n\t"				\
		"mov.l	@r15+, r8\n\t"				\
		"lds.l	@r15+, pr\n\t"				\
		"ldc.l	@r15+, gbr\n\t"				\
		: "=z" (__last)					\
		: "r" (__ts1), "r" (__ts2), "r" (__ts4),	\
		  "r" (__ts5), "r" (__ts6), "r" (__ts7)		\
		: "r3", "t");					\
								\
	if (__ltcb->tsp == &__last->thread &&			\
	    is_dsp_enabled(__last))				\
		__restore_dsp(__last);				\
								\
	__last;							\
	})

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

	do_switch_threads(out_tcb, in_tcb, prev, next);
}

asmlinkage void xnarch_thread_trampoline(struct xnarchtcb *tcb)
{
	xnpod_welcome_thread(tcb->self, tcb->imask);
	tcb->entry(tcb->cookie);
	xnpod_delete_thread(tcb->self);
}

void xnarch_init_thread(struct xnarchtcb *tcb,
			void (*entry)(void *), void *cookie, int imask,
			struct xnthread *thread, char *name)
{
	unsigned long *sp, sr, gbr;

	/*
	 * Stack space is guaranteed to have been fully zeroed. We do
	 * this earlier in xnthread_init() which runs with interrupts
	 * on, to reduce latency.
	 */
	sp = (void *)tcb->stackbase + tcb->stacksize;
	*--sp = (unsigned long)tcb;
	sr = SR_MD;
#ifdef CONFIG_SH_FPU
	sr |= SR_FD;	/* Disable FPU */
#endif
	*--sp = (unsigned long)sr;
	__asm__ __volatile__ ("stc gbr, %0" : "=r" (gbr));
	*--sp = (unsigned long)gbr;
	tcb->ts.sp = (unsigned long)sp;
	tcb->ts.pc = (unsigned long)__asm_thread_trampoline;
	tcb->entry = entry;
	tcb->cookie = cookie;
	tcb->self = thread;
	tcb->imask = imask;
	tcb->name = name;
}

#ifdef CONFIG_XENO_HW_FPU

#define FPSCR_RCHG 0x00000000

static inline get_fpu_owner(struct task_struct *p)
{
	unsigned long __sr;
	__asm__ __volatile__("stc	sr, %0\n\t"
			     : "=&r" (__sr)
			     : /* empty */);
	return (__sr & SR_FD) ? NULL : cur;
}

static inline void do_save_fpu(struct thread_struct *ts)
{
	unsigned long dummy;

	enable_fpu();
	asm volatile ("sts.l	fpul, @-%0\n\t"
		      "sts.l	fpscr, @-%0\n\t"
		      "lds	%2, fpscr\n\t"
		      "frchg\n\t"
		      "fmov.s	fr15, @-%0\n\t"
		      "fmov.s	fr14, @-%0\n\t"
		      "fmov.s	fr13, @-%0\n\t"
		      "fmov.s	fr12, @-%0\n\t"
		      "fmov.s	fr11, @-%0\n\t"
		      "fmov.s	fr10, @-%0\n\t"
		      "fmov.s	fr9, @-%0\n\t"
		      "fmov.s	fr8, @-%0\n\t"
		      "fmov.s	fr7, @-%0\n\t"
		      "fmov.s	fr6, @-%0\n\t"
		      "fmov.s	fr5, @-%0\n\t"
		      "fmov.s	fr4, @-%0\n\t"
		      "fmov.s	fr3, @-%0\n\t"
		      "fmov.s	fr2, @-%0\n\t"
		      "fmov.s	fr1, @-%0\n\t"
		      "fmov.s	fr0, @-%0\n\t"
		      "frchg\n\t"
		      "fmov.s	fr15, @-%0\n\t"
		      "fmov.s	fr14, @-%0\n\t"
		      "fmov.s	fr13, @-%0\n\t"
		      "fmov.s	fr12, @-%0\n\t"
		      "fmov.s	fr11, @-%0\n\t"
		      "fmov.s	fr10, @-%0\n\t"
		      "fmov.s	fr9, @-%0\n\t"
		      "fmov.s	fr8, @-%0\n\t"
		      "fmov.s	fr7, @-%0\n\t"
		      "fmov.s	fr6, @-%0\n\t"
		      "fmov.s	fr5, @-%0\n\t"
		      "fmov.s	fr4, @-%0\n\t"
		      "fmov.s	fr3, @-%0\n\t"
		      "fmov.s	fr2, @-%0\n\t"
		      "fmov.s	fr1, @-%0\n\t"
		      "fmov.s	fr0, @-%0\n\t"
		      "lds	%3, fpscr\n\t":"=r" (dummy)
		      :"0"((char *)(&ts->fpu.hard.status)),
		      "r"(FPSCR_RCHG), "r"(FPSCR_INIT)
		      :"memory");
}

static inline void do_restore_fpu(struct thread_struct *ts)
{
	unsigned long dummy;

	enable_fpu();
	asm volatile ("lds	%2, fpscr\n\t"
		      "fmov.s	@%0+, fr0\n\t"
		      "fmov.s	@%0+, fr1\n\t"
		      "fmov.s	@%0+, fr2\n\t"
		      "fmov.s	@%0+, fr3\n\t"
		      "fmov.s	@%0+, fr4\n\t"
		      "fmov.s	@%0+, fr5\n\t"
		      "fmov.s	@%0+, fr6\n\t"
		      "fmov.s	@%0+, fr7\n\t"
		      "fmov.s	@%0+, fr8\n\t"
		      "fmov.s	@%0+, fr9\n\t"
		      "fmov.s	@%0+, fr10\n\t"
		      "fmov.s	@%0+, fr11\n\t"
		      "fmov.s	@%0+, fr12\n\t"
		      "fmov.s	@%0+, fr13\n\t"
		      "fmov.s	@%0+, fr14\n\t"
		      "fmov.s	@%0+, fr15\n\t"
		      "frchg\n\t"
		      "fmov.s	@%0+, fr0\n\t"
		      "fmov.s	@%0+, fr1\n\t"
		      "fmov.s	@%0+, fr2\n\t"
		      "fmov.s	@%0+, fr3\n\t"
		      "fmov.s	@%0+, fr4\n\t"
		      "fmov.s	@%0+, fr5\n\t"
		      "fmov.s	@%0+, fr6\n\t"
		      "fmov.s	@%0+, fr7\n\t"
		      "fmov.s	@%0+, fr8\n\t"
		      "fmov.s	@%0+, fr9\n\t"
		      "fmov.s	@%0+, fr10\n\t"
		      "fmov.s	@%0+, fr11\n\t"
		      "fmov.s	@%0+, fr12\n\t"
		      "fmov.s	@%0+, fr13\n\t"
		      "fmov.s	@%0+, fr14\n\t"
		      "fmov.s	@%0+, fr15\n\t"
		      "frchg\n\t"
		      "lds.l	@%0+, fpscr\n\t"
		      "lds.l	@%0+, fpul\n\t"
		      :"=r" (dummy)
		      :"0"(&ts->fpu), "r"(FPSCR_RCHG)
		      :"memory");
}

static inline void do_init_fpu(struct thread_struct *ts)
{
	do_restore_fpu(ts);
}

void xnarch_init_fpu(struct xnarchtcb *tcb)
{
	/*
	 * Initialize the FPU for an emerging kernel-based RT
	 * thread. This must be run on behalf of the emerging thread.
	 * xnarch_init_tcb() guarantees that all FPU regs are zeroed
	 * in tcb.
	 */
	do_init_fpu(&tcb->ts);
}

inline void xnarch_enable_fpu(struct xnarchtcb *tcb)
{
	struct task_struct *task = tcb->user_task;

	if (task && task != tcb->user_fpu_owner)
		disable_fpu();
	else
		enable_fpu();
}

void xnarch_save_fpu(struct xnarchtcb *tcb)
{
	struct pt_regs *regs;

	if (tcb->fpup) {
		do_save_fpu(tcb->fpup);
		if (tcb->user_fpu_owner) {
			regs = task_pt_regs(tcb->user_fpu_owner);
			regs->sr |= SR_FD;
		}
	}
}

void xnarch_restore_fpu(struct xnarchtcb *tcb)
{
	struct pt_regs *regs;

	if (tcb->fpup) {
		do_restore_fpu(tcb->fpup);
		/*
		 * Note: Only enable FPU in SR, if it was enabled when
		 * we saved the fpu state.
		 */
		if (tcb->user_fpu_owner) {
			regs = task_pt_regs(tcb->user_fpu_owner);
			regs->sr &= ~SR_FD;
		}
	}

	if (tcb->user_task && tcb->user_task != tcb->user_fpu_owner)
		disable_fpu();
}

#endif /* CONFIG_XENO_HW_FPU */

void xnarch_leave_root(struct xnarchtcb *rootcb)
{
	struct task_struct *p = current;

	rootcb->user_task = rootcb->active_task = p;
	rootcb->tsp = &p->thread;
	rootcb->mm = rootcb->active_mm = ipipe_get_active_mm();
#ifdef CONFIG_XENO_HW_FPU
	rootcb->user_fpu_owner = get_fpu_owner(p);
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

void xnarch_init_root_tcb(struct xnarchtcb *tcb,
			  struct xnthread *thread, const char *name)
{
	tcb->user_task = current;
	tcb->active_task = NULL;
	tcb->tsp = &tcb->ts;
	tcb->mm = current->mm;
	tcb->active_mm = NULL;
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
			    struct xnthread *thread, const char *name)
{
	struct task_struct *task = current;

	tcb->user_task = task;
	tcb->active_task = NULL;
	tcb->tsp = &task->thread;
	tcb->mm = task->mm;
	tcb->active_mm = NULL;
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

void xnarch_init_tcb(struct xnarchtcb *tcb)
{
	tcb->user_task = NULL;
	tcb->active_task = NULL;
	tcb->tsp = &tcb->ts;
	tcb->mm = NULL;
	tcb->active_mm = NULL;
	memset(&tcb->ts, 0, sizeof(tcb->ts));
#ifdef CONFIG_XENO_HW_FPU
	tcb->user_fpu_owner = NULL;
	tcb->fpup = &tcb->ts;
#endif /* CONFIG_XENO_HW_FPU */
}

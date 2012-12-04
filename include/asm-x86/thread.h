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

#ifndef _XENO_ASM_X86_THREAD_H
#define _XENO_ASM_X86_THREAD_H

#ifndef __KERNEL__
#error "Pure kernel header included from user-space!"
#endif

#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/xenomai/wrappers.h>

struct xnthread;
struct task_struct;

struct xnarchtcb {
	x86_fpustate i387 __attribute__ ((aligned (16)));
	unsigned int stacksize;
	unsigned long *stackbase;
#ifdef CONFIG_X86_32
	unsigned long esp;
	unsigned long eip;
#else /* CONFIG_X86_64 */
	unsigned long rsp;
	unsigned long rip;
	unsigned long *rspp;
	unsigned long *ripp;
#ifdef CONFIG_CC_STACKPROTECTOR
	unsigned long canary;
#endif
#endif /* CONFIG_X86_64 */
	struct {
		unsigned long eip;
		unsigned long eax;
#ifdef CONFIG_X86_32
		unsigned long esp;
#endif
	} mayday;

	struct task_struct *user_task;
	struct task_struct *active_task;

	unsigned long *espp;
	unsigned long *eipp;
	x86_fpustate *fpup;

	unsigned is_root: 1;
	unsigned ts_usedfpu: 1;
	unsigned cr0_ts: 1;

	struct xnthread *self;
	int imask;
	const char *name;
	void (*entry)(void *cookie);
	void *cookie;
};

#ifdef CONFIG_X86_32
#ifdef CONFIG_CC_STACKPROTECTOR
#warning "Xenomai: buffer overflow detection not supported in 32bit mode"
#error "           please disable CONFIG_CC_STACKPROTECTOR"
#endif
#if __GNUC__ < 3 || __GNUC__ == 3 && __GNUC_MINOR__ < 2
#warning "Xenomai: outdated gcc/x86_32 release detected"
#error "           please upgrade to gcc 3.2 or later"
#endif

#define XNARCH_THREAD_STACKSZ 4096

static inline int xnarch_shadow_p(struct xnarchtcb *tcb, struct task_struct *task)
{
	return tcb->espp == &task->thread.sp;
}

#else /* CONFIG_X86_64 */

#define XNARCH_THREAD_STACKSZ 8192

#endif /* CONFIG_X86_64 */

#define xnarch_stack_size(tcb)  ((tcb)->stacksize)
#define xnarch_stack_base(tcb)	((tcb)->stackbase)
#define xnarch_stack_end(tcb)	((caddr_t)(tcb)->stackbase - (tcb)->stacksize)
#define xnarch_fpu_ptr(tcb)     ((tcb)->fpup)
#define xnarch_user_task(tcb)   ((tcb)->user_task)
#define xnarch_user_pid(tcb)    ((tcb)->user_task->pid)

#define xnarch_fault_regs(d)	((d)->regs)
#define xnarch_fault_trap(d)	((d)->exception)
#define xnarch_fault_code(d)	((d)->regs->orig_ax)
#define xnarch_fault_pc(d)	((d)->regs->ip)
/* fault is caused by use FPU while FPU disabled. */
#define xnarch_fault_fpu_p(d)	((d)->exception == 7)
/* The following predicates are only usable over a regular Linux stack
   context. */
#define xnarch_fault_pf_p(d)	((d)->exception == 14)
#define xnarch_fault_bp_p(d)	((current->ptrace & PT_PTRACED) &&	\
				 ((d)->exception == 1 || (d)->exception == 3))
#define xnarch_fault_notify(d)	(!xnarch_fault_bp_p(d))

#ifdef CONFIG_XENO_HW_FPU
#define xnarch_fpu_init_p(task)   tsk_used_math(task)
void xnarch_init_fpu(struct xnarchtcb *tcb);
void xnarch_save_fpu(struct xnarchtcb *tcb);
void xnarch_restore_fpu(struct xnarchtcb *tcb);
void xnarch_enable_fpu(struct xnarchtcb *tcb);
#else /* !CONFIG_XENO_HW_FPU */
static inline void xnarch_init_fpu(struct xnarchtcb *tcb) { }
static inline void xnarch_save_fpu(struct xnarchtcb *tcb) { }
static inline void xnarch_restore_fpu(struct xnarchtcb *tcb) { }
static inline void xnarch_enable_fpu(struct xnarchtcb *tcb) { }
#endif /* !CONFIG_XENO_HW_FPU */

void xnarch_switch_to(struct xnarchtcb *out_tcb, struct xnarchtcb *in_tcb);

void xnarch_init_thread(struct xnarchtcb *tcb,
			void (*entry)(void *),
			void *cookie,
			int imask,
			struct xnthread *thread, char *name);

static inline void xnarch_enter_root(struct xnarchtcb *tcb) { }

void xnarch_leave_root(struct xnarchtcb *rootcb);

int xnarch_escalate(void);

void xnarch_init_root_tcb(struct xnarchtcb *tcb,
			  struct xnthread *thread,
			  const char *name);

void xnarch_init_shadow_tcb(struct xnarchtcb *tcb,
			    struct xnthread *thread,
			    const char *name);

void xnarch_init_tcb(struct xnarchtcb *tcb);

int xnarch_alloc_stack(struct xnarchtcb *tcb, size_t stacksize);

void xnarch_free_stack(struct xnarchtcb *tcb);

#endif /* !_XENO_ASM_X86_THREAD_H */

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

#ifndef _XENO_ASM_NIOS2_SYSTEM_H
#define _XENO_ASM_NIOS2_SYSTEM_H

#ifdef __KERNEL__

#include <linux/ptrace.h>
#include <asm-generic/xenomai/system.h>
#include <asm/system.h>
#include <asm/processor.h>

#ifndef CONFIG_MMU
#error "Please use Xenomai 2.5.x for MMU-less support"
#endif

#define XNARCH_THREAD_STACKSZ   4096

#define xnarch_stack_size(tcb)  ((tcb)->stacksize)
#define xnarch_stack_base(tcb)	((tcb)->stackbase)
#define xnarch_stack_end(tcb)	((caddr_t)(tcb)->stackbase - (tcb)->stacksize)
#define xnarch_user_task(tcb)   ((tcb)->user_task)
#define xnarch_user_pid(tcb)    ((tcb)->user_task->pid)

struct xnthread;
struct task_struct;

typedef struct xnarchtcb {	/* Per-thread arch-dependent block */

#define xnarch_fpu_ptr(tcb)     NULL /* No FPU handling at all. */

	unsigned int stacksize;		/* Aligned size of stack */
	unsigned long *stackbase;	/* Stack space */

	struct task_struct *user_task;	/* Shadowed user-space task */
	struct task_struct *active_task;	/* Active user-space task */
	struct thread_struct *tsp;	/* Active thread struct */
	struct mm_struct *mm;
	struct mm_struct *active_mm;
	struct thread_struct ts;	/* Holds kernel-based thread context. */
#ifdef CONFIG_XENO_HW_UNLOCKED_SWITCH
	struct thread_info *tip; /* Pointer to the active thread info (ti or user->thread_info). */
	struct thread_info ti;	/* Holds kernel-based thread info */
#endif
	struct {
		unsigned long ea;
		unsigned long r2;
		unsigned long r3;
	} mayday;

	/* Init block */
	struct xnthread *self;
	int imask;
	const char *name;
	void (*entry)(void *cookie);
	void *cookie;

} xnarchtcb_t;

#define xnarch_fault_trap(d)   ((d)->exception)
#define xnarch_fault_code(d)   (0) /* None on this arch. */
#define xnarch_fault_pc(d)     ((d)->regs->ea)
#define xnarch_fault_fpu_p(d)  (0) /* Can't be. */
/*
 * The following predicates are only usable over a regular Linux stack
 * context.
 */
#define xnarch_fault_pf_p(d)   (0) /* No page faults. */
#define xnarch_fault_bp_p(d)   ((current->ptrace & PT_PTRACED) &&	\
				((d)->exception == IPIPE_TRAP_BP))

#define xnarch_fault_notify(d) (!xnarch_fault_bp_p(d))

#else /* !__KERNEL__ */

#include <nucleus/system.h>
#include <bits/local_lim.h>

#endif /* __KERNEL__ */

#endif /* !_XENO_ASM_NIOS2_SYSTEM_H */

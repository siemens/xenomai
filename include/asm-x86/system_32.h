/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) 2004 The HYADES Project (http://www.hyades-itea.org).
 * Copyright (C) 2004,2005 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
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

#ifndef _XENO_ASM_X86_SYSTEM_32_H
#define _XENO_ASM_X86_SYSTEM_32_H
#define _XENO_ASM_X86_SYSTEM_H

#ifdef __KERNEL__

#include <linux/ptrace.h>
#include <asm-generic/xenomai/system.h>

#define XNARCH_THREAD_STACKSZ 4096

#define xnarch_stack_size(tcb)  ((tcb)->stacksize)
#define xnarch_stack_base(tcb)	((tcb)->stackbase)
#define xnarch_stack_end(tcb)	((caddr_t)(tcb)->stackbase - (tcb)->stacksize)
#define xnarch_fpu_ptr(tcb)     ((tcb)->fpup)
#define xnarch_user_task(tcb)   ((tcb)->user_task)
#define xnarch_user_pid(tcb)    ((tcb)->user_task->pid)

struct xnthread;
struct task_struct;

typedef struct xnarchtcb {      /* Per-thread arch-dependent block */

	/* Kernel mode side */
	x86_fpustate fpuenv __attribute__ ((aligned (16))); /* FPU backup area */
	unsigned stacksize;         /* Aligned size of stack (bytes) */
	unsigned long *stackbase;   /* Stack space */
	unsigned long esp;          /* Saved ESP for kernel-based threads */
	unsigned long eip;          /* Saved EIP for kernel-based threads */
	struct {
		unsigned long eip;
		unsigned long eax;
	} mayday;

	/* User mode side */
	struct task_struct *user_task;      /* Shadowed user-space task */
	struct task_struct *active_task;    /* Active user-space task */

	unsigned long *espp;        /* Pointer to ESP backup area (&esp or &user->thread.esp) */
	unsigned long *eipp;        /* Pointer to EIP backup area (&eip or &user->thread.eip) */
	x86_fpustate *fpup;		/* Pointer to the FPU backup area (&fpuenv or &user->thread.i387.f[x]save */

	/* FPU context bits for root thread. */
	unsigned is_root: 1;
	unsigned ts_usedfpu: 1;
	unsigned cr0_ts: 1;

} xnarchtcb_t;

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

static inline int xnarch_shadow_p (xnarchtcb_t *tcb, struct task_struct *task)
{
    return tcb->espp == &task->thread.sp;
}

#ifdef __cplusplus
}
#endif

#else /* !__KERNEL__ */

#include <nucleus/system.h>
#include <bits/local_lim.h>

#endif /* __KERNEL__ */

#endif /* !_XENO_ASM_X86_SYSTEM_32_H */

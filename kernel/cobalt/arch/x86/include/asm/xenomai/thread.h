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
#ifndef _COBALT_X86_ASM_THREAD_H
#define _COBALT_X86_ASM_THREAD_H

#ifndef __KERNEL__
#error "Pure kernel header included from user-space!"
#endif

#include <asm-generic/xenomai/thread.h>
#include <asm/xenomai/wrappers.h>

struct xnarchtcb {
	x86_fpustate i387 __attribute__ ((aligned (16)));
	struct xntcb core;
	unsigned long sp;
	unsigned long *spp;
	unsigned long ip;
	unsigned long *ipp;
	x86_fpustate *fpup;
	unsigned int is_root: 1;
	unsigned int root_kfpu: 1;
	unsigned int root_used_math: 1;
	struct {
		unsigned long ip;
		unsigned long ax;
		unsigned long sp;
	} mayday;
};

static inline int xnarch_shadow_p(struct xnarchtcb *tcb, struct task_struct *task)
{
	return tcb->spp == &task->thread.sp;
}

#define xnarch_fpu_ptr(tcb)     ((tcb)->fpup)

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

void xnarch_save_fpu(struct xnarchtcb *tcb);
void xnarch_restore_fpu(struct xnarchtcb *tcb);
void xnarch_enable_fpu(struct xnarchtcb *tcb);
int xnarch_handle_fpu_fault(struct xnarchtcb *tcb);

#else /* !CONFIG_XENO_HW_FPU */

static inline void xnarch_save_fpu(struct xnarchtcb *tcb) { }
static inline void xnarch_restore_fpu(struct xnarchtcb *tcb) { }
static inline void xnarch_enable_fpu(struct xnarchtcb *tcb) { }

static inline int xnarch_handle_fpu_fault(struct xnarchtcb *tcb)
{
	return 0;
}

#endif /* !CONFIG_XENO_HW_FPU */

void xnarch_init_root_tcb(struct xnarchtcb *tcb);

void xnarch_init_shadow_tcb(struct xnarchtcb *tcb);

void xnarch_switch_to(struct xnarchtcb *out_tcb, struct xnarchtcb *in_tcb);

static inline void xnarch_enter_root(struct xnarchtcb *tcb) { }

void xnarch_leave_root(struct xnarchtcb *rootcb);

int xnarch_escalate(void);

#endif /* !_COBALT_X86_ASM_THREAD_H */

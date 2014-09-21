/*
 * Copyright (C) 2011,2013 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _COBALT_SH_ASM_THREAD_H
#define _COBALT_SH_ASM_THREAD_H

#include <asm-generic/xenomai/thread.h>

struct xnarchtcb {
	struct xntcb core;
#ifdef CONFIG_XENO_ARCH_FPU
	struct thread_struct *fpup;
#define xnarch_fpu_ptr(tcb)     ((tcb)->fpup)
#else
#define xnarch_fpu_ptr(tcb)     NULL
#endif
	struct {
		unsigned long pc;
		unsigned long r3;
	} mayday;
};

#define xnarch_fault_trap(d)   ((d)->exception)
#define xnarch_fault_code(d)   0
#define xnarch_fault_pc(d)     ((d)->regs->pc)
#define xnarch_fault_fpu_p(d)  ((d)->exception == IPIPE_TRAP_FPUERR)
#define xnarch_fault_pf_p(d)   ((d)->exception == IPIPE_TRAP_PF)
#define xnarch_fault_bp_p(d)   ((current->ptrace & PT_PTRACED) &&	\
				(d)->exception == IPIPE_TRAP_BP)
#define xnarch_fault_notify(d) (xnarch_fault_bp_p(d) == 0)

static inline void xnarch_enter_root(struct xnthread *root) { }

#ifdef CONFIG_XENO_ARCH_FPU

void xnarch_leave_root(struct xnthread *root);

static inline void xnarch_init_root_tcb(struct xnthread *thread)
{
	struct xnarchtcb *tcb = xnthread_archtcb(thread);
	tcb->fpup = NULL;
}

void xnarch_init_shadow_tcb(struct xnthread *thread);

#else /* !CONFIG_XENO_ARCH_FPU */

static inline void xnarch_leave_root(struct xnthread *root) { }
static inline void xnarch_init_root_tcb(struct xnthread *thread) { }
static inline void xnarch_init_shadow_tcb(struct xnthread *thread) { }

#endif /* !CONFIG_XENO_ARCH_FPU */

static inline int 
xnarch_handle_fpu_fault(struct xnthread *from, 
			struct xnthread *to, struct ipipe_trap_data *d)
{
	return 0;
}

#endif /* !_COBALT_SH_ASM_THREAD_H */

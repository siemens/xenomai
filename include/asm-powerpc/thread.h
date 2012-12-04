/*
 * Copyright (C) 2004-2006 Philippe Gerum.
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

#ifndef _XENO_ASM_POWERPC_THREAD_H
#define _XENO_ASM_POWERPC_THREAD_H

#ifndef __KERNEL__
#error "Pure kernel header included from user-space!"
#endif

#include <asm/ptrace.h>
#include <asm/processor.h>

struct xnthread;
struct task_struct;

struct xnarchtcb {

	/* User mode side */
	struct task_struct *user_task; /* Shadowed user-space task */
	struct task_struct *active_task; /* Active user-space task */
	struct thread_struct *tsp;	/* Pointer to the active thread struct (&ts or &user->thread). */
	struct mm_struct *mm;
	struct mm_struct *active_mm;
	struct {
		unsigned long nip;
		unsigned long r0;
	} mayday;

	/* Kernel mode side */
	struct thread_struct ts;	/* Holds kernel-based thread context. */
#ifdef CONFIG_XENO_HW_UNLOCKED_SWITCH
	struct thread_info *tip; /* Pointer to the active thread info (ti or user->thread_info). */
	struct thread_info ti;	/* Holds kernel-based thread info */
#endif
#ifdef CONFIG_XENO_HW_FPU
	/*
	 * We only care for basic FPU handling in kernel-space; Altivec
	 * and SPE are not available to kernel-based nucleus threads.
	 */
	struct thread_struct *fpup;	/* Pointer to the FPU backup container */
	struct task_struct *user_fpu_owner;
	/*
	 * Pointer to the FPU owner in userspace:
	 * - NULL for RT K threads,
	 * - last_task_used_math for Linux US threads (current or NULL when SMP)
	 * - current for RT US threads.
	 */
#define xnarch_fpu_ptr(tcb)     ((tcb)->fpup)
#else
#define xnarch_fpu_ptr(tcb)     NULL
#endif

	unsigned stacksize;	/* Aligned size of stack (bytes) */
	unsigned long *stackbase;	/* Stack space */

	/* Init block */
	struct xnthread *self;
	int imask;
	const char *name;
	void (*entry) (void *cookie);
	void *cookie;
};

#define xnarch_fault_regs(d)	((d)->regs)
#define xnarch_fault_trap(d)    ((unsigned int)(d)->regs->trap)
#define xnarch_fault_code(d)    ((d)->regs->dar)
#define xnarch_fault_pc(d)      ((d)->regs->nip)
#define xnarch_fault_pc(d)      ((d)->regs->nip)
#define xnarch_fault_fpu_p(d)   0
#define xnarch_fault_pf_p(d)   ((d)->exception == IPIPE_TRAP_ACCESS)
#ifdef CONFIG_PPC64
#define XNARCH_THREAD_STACKSZ   8182
#define xnarch_fault_bp_p(d)   ((current->ptrace & PT_PTRACED) &&	\
				((d)->exception == IPIPE_TRAP_IABR ||	\
				 (d)->exception == IPIPE_TRAP_SSTEP))
#else /* !CONFIG_PPC64 */
#define XNARCH_THREAD_STACKSZ   4096
#define xnarch_fault_bp_p(d)   ((current->ptrace & PT_PTRACED) &&	\
				((d)->exception == IPIPE_TRAP_IABR ||	\
				 (d)->exception == IPIPE_TRAP_SSTEP ||	\
				 (d)->exception == IPIPE_TRAP_DEBUG))
#endif /* CONFIG_PPC64 */
#define xnarch_fault_notify(d) (xnarch_fault_bp_p(d) == 0)

#define xnarch_stack_size(tcb)  ((tcb)->stacksize)
#define xnarch_stack_base(tcb)	((tcb)->stackbase)
#define xnarch_stack_end(tcb)	((caddr_t)(tcb)->stackbase - (tcb)->stacksize)
#define xnarch_user_task(tcb)   ((tcb)->user_task)
#define xnarch_user_pid(tcb)    ((tcb)->user_task->pid)

void xnarch_switch_to(struct xnarchtcb *out_tcb, struct xnarchtcb *in_tcb);

void xnarch_init_thread(struct xnarchtcb *tcb,
			void (*entry)(void *),
			void *cookie,
			int imask,
			struct xnthread *thread, char *name);

void xnarch_enter_root(struct xnarchtcb *rootcb);

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

static inline int xnarch_fpu_init_p(struct task_struct *task)
{
	/* No lazy FPU init on PowerPC. */
	return 1;
}

void xnarch_enable_fpu(struct xnarchtcb *current_tcb);

void xnarch_init_fpu(struct xnarchtcb *tcb);

void xnarch_save_fpu(struct xnarchtcb *tcb);

void xnarch_restore_fpu(struct xnarchtcb *tcb);

#endif /* !_XENO_ASM_POWERPC_THREAD_H */

/*
 * Copyright (C) 2005 Stelian Pop
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

#ifndef _XENO_ASM_ARM_THREAD_H
#define _XENO_ASM_ARM_THREAD_H

#ifndef __KERNEL__
#error "Pure kernel header included from user-space!"
#endif

#include <asm/ptrace.h>
#include <asm/processor.h>

struct xnthread;
struct task_struct;

#define XNARCH_THREAD_STACKSZ	4096

#define xnarch_stack_size(tcb)	((tcb)->stacksize)
#define xnarch_stack_base(tcb)	((tcb)->stackbase)
#define xnarch_stack_end(tcb)	((caddr_t)(tcb)->stackbase - (tcb)->stacksize)
#define xnarch_user_task(tcb)	((tcb)->user_task)
#define xnarch_user_pid(tcb)	((tcb)->user_task->pid)

#ifdef CONFIG_XENO_HW_FPU

#ifdef CONFIG_VFP
#include <asm/vfp.h>
#endif /* CONFIG_VFP */

struct arm_fpustate {
	/*
	 * This layout must follow exactely the definition of the FPU
	 * area in the ARM thread_info structure. 'tp_value' is also
	 * saved even if it is not needed, but it shouldn't matter.
	 */
	__u8                    used_cp[16];    /* thread used copro */
	unsigned long           tp_value;
	struct crunch_state     crunchstate;
	union fp_state          fpstate __attribute__((aligned(8)));
	union vfp_state         vfpstate;
};

int xnarch_fault_fpu_p(struct ipipe_trap_data *d);
#else /* !CONFIG_XENO_HW_FPU */
/*
 * Userland may raise FPU faults with FPU-enabled kernels, regardless
 * of whether real-time threads actually use FPU, so we simply ignore
 * these faults.
 */
static inline int xnarch_fault_fpu_p(struct ipipe_trap_data *d)
{
	return 0;
}
#endif /* !CONFIG_XENO_HW_FPU */

struct xnarchtcb {
#ifdef CONFIG_XENO_HW_FPU
	struct arm_fpustate fpuenv;
	struct arm_fpustate *fpup;	/* Pointer to the FPU backup area */
	struct task_struct *user_fpu_owner;
	/*
	 * Pointer the the FPU owner in userspace:
	 * - NULL for RT K threads,
	 * - last_task_used_math for Linux US threads (only current or NULL when MP)
	 * - current for RT US threads.
	 */
	unsigned is_root;
#define xnarch_fpu_ptr(tcb)	((tcb)->fpup)
#else /* !CONFIG_XENO_HW_FPU */
#define xnarch_fpu_ptr(tcb)	NULL
#endif /* CONFIG_XENO_HW_FPU */
	unsigned int stacksize;	    /* Aligned size of stack (bytes) */
	unsigned long *stackbase;   /* Stack space */
	struct task_struct *user_task;	    /* Shadowed user-space task */
	struct task_struct *active_task;    /* Active user-space task */
	struct mm_struct *mm;
	struct mm_struct *active_mm;
	struct thread_info ti;		    /* Holds kernel-based thread info */
	struct thread_info *tip;	    /* Pointer to the active thread info (ti or user->thread_info). */
	struct {
		unsigned long pc;
		unsigned long r0;
#ifdef CONFIG_XENO_ARM_EABI
		unsigned long r7;
#endif
#ifdef CONFIG_ARM_THUMB
		unsigned long psr;
#endif
	} mayday;
	struct xnthread *self;
	int imask;
	const char *name;
	void (*entry)(void *cookie);
	void *cookie;
};

#define xnarch_fault_regs(d)	((d)->regs)
#define xnarch_fault_trap(d)	((d)->exception)
#define xnarch_fault_code(d)	(0)
#define xnarch_fault_pc(d)	((d)->regs->ARM_pc - (thumb_mode((d)->regs) ? 2 : 4)) /* XXX ? */

#define xnarch_fault_pf_p(d)	((d)->exception == IPIPE_TRAP_ACCESS)
#define xnarch_fault_bp_p(d)	((current->ptrace & PT_PTRACED) &&	\
				 ((d)->exception == IPIPE_TRAP_BREAK))

#define xnarch_fault_notify(d) (!xnarch_fault_bp_p(d))

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
	/* No lazy FPU init on ARM. */
	return 1;
}

void xnarch_enable_fpu(struct xnarchtcb *current_tcb);

void xnarch_init_fpu(struct xnarchtcb *tcb);

void xnarch_save_fpu(struct xnarchtcb *tcb);

void xnarch_restore_fpu(struct xnarchtcb *tcb);

#endif /* !_XENO_ASM_ARM_THREAD_H */

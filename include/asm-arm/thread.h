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

#include <asm-generic/xenomai/thread.h>

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
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0) || defined(CONFIG_CRUNCH)
	struct crunch_state     crunchstate;
#endif
	union fp_state          fpstate __attribute__((aligned(8)));
	union vfp_state         vfpstate;
};

#endif /* !CONFIG_XENO_HW_FPU */

struct xnarchtcb {
	struct xntcb core;
#ifdef CONFIG_XENO_HW_FPU
	struct arm_fpustate fpuenv;
	struct arm_fpustate *fpup;
	unsigned int is_root;
#define xnarch_fpu_ptr(tcb)     ((tcb)->fpup)
#else
#define xnarch_fpu_ptr(tcb)     NULL
#endif
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
};

#define xnarch_fault_regs(d)	((d)->regs)
#define xnarch_fault_trap(d)	((d)->exception)
#define xnarch_fault_code(d)	(0)
#define xnarch_fault_pc(d)	((d)->regs->ARM_pc - (thumb_mode((d)->regs) ? 2 : 4)) /* XXX ? */

#define xnarch_fault_pf_p(d)	((d)->exception == IPIPE_TRAP_ACCESS)
#define xnarch_fault_bp_p(d)	((current->ptrace & PT_PTRACED) &&	\
				 ((d)->exception == IPIPE_TRAP_BREAK))

#define xnarch_fault_notify(d) (!xnarch_fault_bp_p(d))

void xnarch_enable_fpu(struct xnarchtcb *current_tcb);

void xnarch_save_fpu(struct xnarchtcb *tcb);

void xnarch_restore_fpu(struct xnarchtcb *tcb);

void xnarch_switch_to(struct xnarchtcb *out_tcb, struct xnarchtcb *in_tcb);

static inline void xnarch_enter_root(struct xnarchtcb *rootcb) { }

int xnarch_escalate(void);

#ifdef CONFIG_XENO_HW_FPU

static inline void xnarch_init_root_tcb(struct xnarchtcb *tcb)
{
	tcb->fpup = NULL;
}

static inline void xnarch_init_shadow_tcb(struct xnarchtcb *tcb)
{
	tcb->fpup = (struct arm_fpustate *)
		&task_thread_info(tcb->core.host_task)->used_cp[0];
}

int xnarch_fault_fpu_p(struct ipipe_trap_data *d);

void xnarch_leave_root(struct xnarchtcb *rootcb);

#else /* !CONFIG_XENO_HW_FPU */

static inline void xnarch_init_root_tcb(struct xnarchtcb *tcb) { }
static inline void xnarch_init_shadow_tcb(struct xnarchtcb *tcb) { }

/*
 * Userland may raise FPU faults with FPU-enabled kernels, regardless
 * of whether real-time threads actually use FPU, so we simply ignore
 * these faults.
 */
static inline int xnarch_fault_fpu_p(struct ipipe_trap_data *d)
{
	return 0;
}

static inline void xnarch_leave_root(struct xnarchtcb *rootcb) { }

#endif /* !CONFIG_XENO_HW_FPU */

static inline int xnarch_handle_fpu_fault(struct xnarchtcb *tcb)
{
	return 0;
}

#endif /* !_XENO_ASM_ARM_THREAD_H */

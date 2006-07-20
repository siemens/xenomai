/*
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
 *
 * ARM port
 *   Copyright (C) 2005 Stelian Pop
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

#ifndef _XENO_ASM_ARM_BITS_SHADOW_H
#define _XENO_ASM_ARM_BITS_SHADOW_H

#ifndef __KERNEL__
#error "Pure kernel header included from user-space!"
#endif

static inline void xnarch_init_shadow_tcb(xnarchtcb_t * tcb,
					  struct xnthread *thread,
					  const char *name)
{
	struct task_struct *task = current;

	tcb->user_task = task;
	tcb->active_task = NULL;
	tcb->tip = task->thread_info;
#ifdef CONFIG_XENO_HW_FPU
	tcb->user_fpu_owner = task;
	tcb->fpup = (rthal_fpenv_t *) & task->thread_info->used_cp[0];
#endif /* CONFIG_XENO_HW_FPU */
	tcb->entry = NULL;
	tcb->cookie = NULL;
	tcb->self = thread;
	tcb->imask = 0;
	tcb->name = name;
}

static inline void xnarch_grab_xirqs(rthal_irq_handler_t handler)
{
	unsigned irq;

	for (irq = 0; irq < IPIPE_NR_XIRQS; irq++)
		rthal_virtualize_irq(rthal_current_domain,
				     irq,
				     handler, NULL, NULL, IPIPE_HANDLE_MASK);
}

static inline void xnarch_lock_xirqs(rthal_pipeline_stage_t * ipd, int cpuid)
{
	unsigned irq;

	for (irq = 0; irq < IPIPE_NR_XIRQS; irq++)
		rthal_lock_irq(ipd, cpuid, irq);

}

static inline void xnarch_unlock_xirqs(rthal_pipeline_stage_t * ipd, int cpuid)
{
	unsigned irq;

	for (irq = 0; irq < IPIPE_NR_XIRQS; irq++)
		rthal_unlock_irq(ipd, irq);
}

static inline int xnarch_local_syscall(struct pt_regs *regs)
{
	int error = 0;

	switch (__xn_reg_arg1(regs)) {
	case XENOMAI_SYSARCH_ATOMIC_ADD_RETURN:{
			int i;
			atomic_t *v, val;
			int ret;
			unsigned long flags;

			local_irq_save_hw(flags);
			__xn_get_user(current, i, (int *)__xn_reg_arg2(regs));
			__xn_get_user(current, v,
				      (atomic_t **) __xn_reg_arg3(regs));
			__xn_copy_from_user(current, &val, v, sizeof(atomic_t));
			ret = atomic_add_return(i, &val);
			__xn_copy_to_user(current, v, &val, sizeof(atomic_t));
			__xn_put_user(current, ret, (int *)__xn_reg_arg4(regs));
			local_irq_restore_hw(flags);
			break;
		}
	case XENOMAI_SYSARCH_ATOMIC_SET_MASK:{
			unsigned long mask;
			unsigned long *addr, val;
			unsigned long flags;

			local_irq_save_hw(flags);
			__xn_get_user(current, mask,
				      (unsigned long *)__xn_reg_arg2(regs));
			__xn_get_user(current, addr,
				      (unsigned long **)__xn_reg_arg3(regs));
			__xn_get_user(current, val, (unsigned long *)addr);
			val |= mask;
			__xn_put_user(current, val, (unsigned long *)addr);
			local_irq_restore_hw(flags);
			break;
		}
	case XENOMAI_SYSARCH_ATOMIC_CLEAR_MASK:{
			unsigned long mask;
			unsigned long *addr, val;
			unsigned long flags;

			local_irq_save_hw(flags);
			__xn_get_user(current, mask,
				      (unsigned long *)__xn_reg_arg2(regs));
			__xn_get_user(current, addr,
				      (unsigned long **)__xn_reg_arg3(regs));
			__xn_get_user(current, val, (unsigned long *)addr);
			val &= ~mask;
			__xn_put_user(current, val, (unsigned long *)addr);
			local_irq_restore_hw(flags);
			break;
		}
	case XENOMAI_SYSARCH_XCHG:{
			void *ptr;
			unsigned long x;
			unsigned int size;
			unsigned long ret = 0;
			unsigned long flags;

			local_irq_save_hw(flags);
			__xn_get_user(current, ptr,
				      (unsigned char **)__xn_reg_arg2(regs));
			__xn_get_user(current, x,
				      (unsigned long *)__xn_reg_arg3(regs));
			__xn_get_user(current, size,
				      (unsigned int *)__xn_reg_arg4(regs));
			if (size == 4) {
				unsigned long val;
				__xn_get_user(current, val,
					      (unsigned long *)ptr);
				ret = xnarch_atomic_xchg(&val, x);
			} else
				error = -EINVAL;
			__xn_put_user(current, ret,
				      (unsigned long *)__xn_reg_arg5(regs));
			local_irq_restore_hw(flags);
			break;
		}
	default:
		error = -EINVAL;
	}
	return error;
}

#endif /* !_XENO_ASM_ARM_BITS_SHADOW_H */

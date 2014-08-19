/*
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
 *
 * ARM port
 *   Copyright (C) 2005 Stelian Pop
 *
 * Xenomai is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
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
#ifndef _COBALT_ARM_ASM_SYSCALL_H
#define _COBALT_ARM_ASM_SYSCALL_H

#include <linux/errno.h>
#include <asm/uaccess.h>
#include <asm/ptrace.h>
#include <asm-generic/xenomai/syscall.h>

#define __xn_reg_sys(regs)      ((regs)->ARM_ORIG_r0)
#define __xn_reg_rval(regs)     ((regs)->ARM_r0)
#define __xn_reg_arg1(regs)     ((regs)->ARM_r1)
#define __xn_reg_arg2(regs)     ((regs)->ARM_r2)
#define __xn_reg_arg3(regs)     ((regs)->ARM_r3)
#define __xn_reg_arg4(regs)     ((regs)->ARM_r4)
#define __xn_reg_arg5(regs)     ((regs)->ARM_r5)
#define __xn_reg_pc(regs)       ((regs)->ARM_ip)
#define __xn_reg_sp(regs)       ((regs)->ARM_sp)

/* In OABI_COMPAT mode, handle both OABI and EABI userspace syscalls */
#ifdef CONFIG_OABI_COMPAT
#define __xn_syscall_p(regs)    ( ((regs)->ARM_r7 == __NR_OABI_SYSCALL_BASE + XENO_ARM_SYSCALL) || \
				  ((regs)->ARM_r7 == __NR_SYSCALL_BASE + XENO_ARM_SYSCALL) )
#define __xn_syslinux_p(regs, nr) \
				( ((regs)->ARM_r7 == __NR_OABI_SYSCALL_BASE + (nr)) || \
				  ((regs)->ARM_r7 == __NR_SYSCALL_BASE + (nr)) )
#else /* !CONFIG_OABI_COMPAT */
#define __xn_syscall_p(regs)      ((regs)->ARM_r7 == __NR_SYSCALL_BASE + XENO_ARM_SYSCALL)
#define __xn_syslinux_p(regs, nr) ((regs)->ARM_r7 == __NR_SYSCALL_BASE + (nr))
#endif /* !CONFIG_OABI_COMPAT */

#define __xn_syscall(regs)       ((__xn_reg_sys(regs) >> 24) & 0xff)

static inline void __xn_success_return(struct pt_regs *regs, int v)
{
	__xn_reg_rval(regs) = v;
}

static inline void __xn_error_return(struct pt_regs *regs, int v)
{
	__xn_reg_rval(regs) = v;
}

static inline void __xn_status_return(struct pt_regs *regs, int v)
{
	__xn_reg_rval(regs) = v;
}

static inline int __xn_interrupted_p(struct pt_regs *regs)
{
	return __xn_reg_rval(regs) == -EINTR;
}

int xnarch_local_syscall(unsigned long a1, unsigned long a2,
			 unsigned long a3, unsigned long a4,
			 unsigned long a5);

#endif /* !_COBALT_ARM_ASM_SYSCALL_H */

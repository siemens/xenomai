/*
 * Copyright (C) 2009 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _COBALT_NIOS2_ASM_SYSCALL_H
#define _COBALT_NIOS2_ASM_SYSCALL_H

#include <linux/errno.h>
#include <asm/uaccess.h>
#include <asm/ptrace.h>
#include <asm-generic/xenomai/syscall.h>

#define __xn_reg_mux(regs)    ((regs)->r2)
#define __xn_reg_rval(regs)   ((regs)->r2)
#define __xn_reg_arg1(regs)   ((regs)->r4)
#define __xn_reg_arg2(regs)   ((regs)->r5)
#define __xn_reg_arg3(regs)   ((regs)->r6)
#define __xn_reg_arg4(regs)   ((regs)->r7)
#define __xn_reg_arg5(regs)   ((regs)->r8)
#define __xn_reg_pc(regs)     ((regs)->ea)
#define __xn_reg_sp(regs)     ((regs)->sp)

#define __xn_reg_mux_p(regs)        ((__xn_reg_mux(regs) & 0xffff) == sc_nucleus_mux)
#define __xn_mux_id(regs)           ((__xn_reg_mux(regs) >> 24) & 0xff)
#define __xn_mux_op(regs)           ((__xn_reg_mux(regs) >> 16) & 0xff)

#define __xn_linux_mux_p(regs, nr)  (__xn_reg_mux(regs) == (nr))

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

#endif /* !_COBALT_NIOS2_ASM_SYSCALL_H */

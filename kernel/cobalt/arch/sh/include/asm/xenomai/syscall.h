/*
 * Copyright (C) 2011 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _COBALT_SH_ASM_SYSCALL_H
#define _COBALT_SH_ASM_SYSCALL_H

#ifndef __KERNEL__
#error "Pure kernel header included from user-space!"
#endif

#include <linux/errno.h>
#include <asm/ptrace.h>
#include <asm-generic/xenomai/syscall.h>

#define __xn_reg_mux(regs)    ((regs)->regs[3])
#define __xn_reg_rval(regs)   ((regs)->regs[0])
#define __xn_reg_arg1(regs)   ((regs)->regs[4])
#define __xn_reg_arg2(regs)   ((regs)->regs[5])
#define __xn_reg_arg3(regs)   ((regs)->regs[6])
#define __xn_reg_arg4(regs)   ((regs)->regs[7])
#define __xn_reg_arg5(regs)   ((regs)->regs[0])

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

static inline int xnarch_local_syscall(void)
{
	return -ENOSYS;
}

#endif /* !_COBALT_SH_ASM_SYSCALL_H */

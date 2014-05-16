/*
 * Copyright (C) 2001,2002,2003,2004,2005 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _COBALT_ASM_GENERIC_SYSCALL_H
#define _COBALT_ASM_GENERIC_SYSCALL_H

#include <linux/types.h>
#include <asm/uaccess.h>
#include <asm/xenomai/features.h>
#include <asm/xenomai/wrappers.h>
#include <asm/xenomai/machine.h>
#include <cobalt/uapi/asm-generic/syscall.h>
#include <cobalt/uapi/signal.h>

struct task_struct;
struct pt_regs;

struct xnsyscall {
	/*
	 * CAUTION: no varargs, we want the calling convention for
	 * regular functions to apply.
	 */
	int (*svc)(unsigned long arg1, unsigned long arg2, unsigned long arg3,
		   unsigned long arg4, unsigned long arg5);

/* Syscall must run into the Linux domain. */
#define __xn_exec_lostage    0x1
/* Syscall must run into the Xenomai domain. */
#define __xn_exec_histage    0x2
/* Shadow syscall; caller must be mapped. */
#define __xn_exec_shadow     0x4
/* Switch back toggle; caller must return to its original mode. */
#define __xn_exec_switchback 0x8
/* Exec in current domain. */
#define __xn_exec_current    0x10
/* Exec in conforming domain, Xenomai or Linux. */
#define __xn_exec_conforming 0x20
/* Attempt syscall restart in the opposite domain upon -ENOSYS. */
#define __xn_exec_adaptive   0x40
/* Do not restart syscall upon signal receipt. */
#define __xn_exec_norestart  0x80
/* Context-agnostic syscall. Will actually run in Xenomai domain. */
#define __xn_exec_any        0x0
/* Shorthand for shadow init syscall. */
#define __xn_exec_init       __xn_exec_lostage
/* Shorthand for shadow syscall in Xenomai space. */
#define __xn_exec_primary   (__xn_exec_shadow|__xn_exec_histage)
/* Shorthand for shadow syscall in Linux space. */
#define __xn_exec_secondary (__xn_exec_shadow|__xn_exec_lostage)
/* Shorthand for syscall in Linux space with switchback if shadow. */
#define __xn_exec_downup    (__xn_exec_lostage|__xn_exec_switchback)
/* Shorthand for non-restartable primary syscall. */
#define __xn_exec_nonrestartable (__xn_exec_primary|__xn_exec_norestart)
/* Shorthand for domain probing syscall */
#define __xn_exec_probing   (__xn_exec_current|__xn_exec_adaptive)
/* Shorthand for oneway trap - does not return to call site. */
#define __xn_exec_oneway    (__xn_exec_any|__xn_exec_norestart)

	unsigned long flags;
};

#define __syscast__(fn)	((int (*)(unsigned long, unsigned long,	\
				  unsigned long, unsigned long, unsigned long))(fn))

#define SKINCALL_DEF(nr, fn, fl)	\
	[nr] = { .svc = __syscast__(fn), .flags = __xn_exec_##fl }

#define SKINCALL_NI	\
	{ .svc = __syscast__(cobalt_syscall_ni), .flags = 0 }

#define access_rok(addr, size)	access_ok(VERIFY_READ, (addr), (size))
#define access_wok(addr, size)	access_ok(VERIFY_WRITE, (addr), (size))

#define __xn_reg_arglist(regs)	\
	__xn_reg_arg1(regs),	\
	__xn_reg_arg2(regs),	\
	__xn_reg_arg3(regs),	\
	__xn_reg_arg4(regs),	\
	__xn_reg_arg5(regs)

#define __xn_copy_from_user(dstP, srcP, n)	__copy_from_user_inatomic(dstP, srcP, n)
#define __xn_copy_to_user(dstP, srcP, n)	__copy_to_user_inatomic(dstP, srcP, n)
#define __xn_put_user(src, dstP)		__put_user_inatomic(src, dstP)
#define __xn_get_user(dst, srcP)		__get_user_inatomic(dst, srcP)
#define __xn_strncpy_from_user(dstP, srcP, n)	strncpy_from_user(dstP, srcP, n)

static inline int __xn_safe_copy_from_user(void *dst, const void __user *src,
					   size_t size)
{
	return (!access_rok(src, size) ||
		__xn_copy_from_user(dst, src, size)) ? -EFAULT : 0;
}

static inline int __xn_safe_copy_to_user(void __user *dst, const void *src,
					 size_t size)
{
	return (!access_wok(dst, size) ||
		__xn_copy_to_user(dst, src, size)) ? -EFAULT : 0;
}

static inline int __xn_safe_strncpy_from_user(char *dst,
					      const char __user *src, size_t count)
{
	if (unlikely(!access_rok(src, 1)))
		return -EFAULT;
	return __xn_strncpy_from_user(dst, src, count);
}

#endif /* !_COBALT_ASM_GENERIC_SYSCALL_H */

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

#ifndef _XENO_ASM_GENERIC_SYSCALL_H
#define _XENO_ASM_GENERIC_SYSCALL_H

#include <asm/xenomai/features.h>

/* Xenomai multiplexer syscall. */
#define __xn_sys_mux		555	/* Must fit within 15bit */
/* Xenomai nucleus syscalls. */
#define __xn_sys_bind		0	/* muxid = bind_to_interface(magic, &breq) */
#define __xn_sys_completion	1	/* xnshadow_completion(&completion) */
#define __xn_sys_migrate	2	/* switched = xnshadow_relax/harden() */
#define __xn_sys_barrier	3	/* started = xnshadow_wait_barrier(&entry,&cookie) */
#define __xn_sys_info		4	/* xnshadow_get_info(muxid,&info) */
#define __xn_sys_arch		5	/* r = xnarch_local_syscall(args) */
#define __xn_sys_trace		6	/* r = xntrace_xxx(...) */
#define __xn_sys_heap_info	7
#define __xn_sys_current	8	/* threadh = xnthread_handle(cur) */
#define __xn_sys_current_info	9	/* r = xnshadow_current_info(&info) */
#define __xn_sys_mayday        10	/* request mayday fixup */
#define __xn_sys_backtrace     11	/* collect backtrace (relax tracing) */

struct xnbindreq {
	int feat_req;		/* Features userland requires. */
	int abi_rev;		/* ABI revision userland uses. */
	struct xnfeatinfo feat_ret; /* Features kernel space provides. */
};

#define XENOMAI_LINUX_DOMAIN  0
#define XENOMAI_XENO_DOMAIN   1

typedef struct xnsysinfo {
	unsigned long long clockfreq;	/* Real-time clock frequency */
	unsigned long tickval;		/* Tick duration (ns) */
	unsigned long vdso;  		/* Offset of nkvdso in the sem heap */
} xnsysinfo_t;

#define SIGSHADOW  SIGWINCH
#define SIGSHADOW_ACTION_HARDEN     1
#define SIGSHADOW_ACTION_RENICE     2
#define SIGSHADOW_ACTION_BACKTRACE  3
#define sigshadow_action(code) ((code) & 0xff)
#define sigshadow_arg(code) (((code) >> 8) & 0xff)
#define sigshadow_int(action, arg) ((action) | ((arg) << 8))

#define SIGSHADOW_BACKTRACE_DEPTH  16

#define SIGDEBUG			SIGXCPU
#define SIGDEBUG_UNDEFINED		0
#define SIGDEBUG_MIGRATE_SIGNAL		1
#define SIGDEBUG_MIGRATE_SYSCALL	2
#define SIGDEBUG_MIGRATE_FAULT		3
#define SIGDEBUG_MIGRATE_PRIOINV	4
#define SIGDEBUG_NOMLOCK		5
#define SIGDEBUG_WATCHDOG		6

#define sigdebug_code(si)	((si)->si_value.sival_int)
#define sigdebug_reason(si)	(sigdebug_code(si) & 0xff)

#ifdef __KERNEL__

#include <linux/types.h>
#include <asm/uaccess.h>
#include <asm/xenomai/wrappers.h>
#include <asm/xenomai/hal.h>

struct task_struct;
struct pt_regs;

#define XENOMAI_MAX_SYSENT 255

struct xnsysent {

	int (*svc)(u_long arg, ...);

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

#define __syscast__(fn)	((int (*)(u_long, ...))(fn))

#define SKINCALL_DEF(nr, fn, fl)	\
	[nr] = { .svc = __syscast__(fn), .flags = __xn_exec_##fl }

extern int nkthrptd;

extern int nkerrptd;

#define xnshadow_thrptd(t) ((t)->ptd[nkthrptd])
#define xnshadow_thread(t) ((xnthread_t *)xnshadow_thrptd(t))
/* The errno field must be addressable for plain Linux tasks too. */
#define xnshadow_errno(t)  (*(int *)&((t)->ptd[nkerrptd]))

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
#define __xn_put_user(src, dstP)		__put_user(src, dstP)
#define __xn_get_user(dst, srcP)		__get_user(dst, srcP)
#define __xn_strncpy_from_user(dstP, srcP, n)	wrap_strncpy_from_user(dstP, srcP, n)

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

#else /* !__KERNEL__ */

#include <sys/types.h>

#endif /* __KERNEL__ */

typedef struct xncompletion {
	long syncflag;		/* Semaphore variable. */
	pid_t pid;		/* Single waiter ID. */
} xncompletion_t;

#endif /* !_XENO_ASM_GENERIC_SYSCALL_H */

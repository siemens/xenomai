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

/* Xenomai multiplexer syscall. */
#define __xn_sys_mux        555	/* Must fit within 15bit */
/* Xenomai nucleus syscalls. */
#define __xn_sys_bind       0	/* muxid = bind_to_interface(magic,infp) */
#define __xn_sys_completion 1	/* xnshadow_completion(&completion) */
#define __xn_sys_migrate    2	/* switched = xnshadow_relax/harden() */
#define __xn_sys_barrier    3	/* started = xnshadow_wait_barrier(&entry,&cookie) */

#define XENOMAI_LINUX_DOMAIN  0
#define XENOMAI_XENO_DOMAIN   1

typedef struct xnsysinfo {

    unsigned long long cpufreq;	/* CPU frequency */
    unsigned long tickval;	/* Tick duration (ns) */

} xnsysinfo_t;

typedef struct xninquiry {

    char name[32];
    int prio;
    unsigned long status;
    void *khandle;
    void *uhandle;

} xninquiry_t;

#ifdef __KERNEL__

#include <linux/types.h>

struct task_struct;

#define XENOMAI_MAX_SYSENT 255

typedef struct _xnsysent {

    int (*svc)(struct task_struct *task,
	       struct pt_regs *regs);

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
/* Context-agnostic syscall. */
#define __xn_exec_any        0x0
/* Short-hand for shadow init syscall. */
#define __xn_exec_init       __xn_exec_lostage
/* Short-hand for shadow syscall in Xenomai space. */
#define __xn_exec_primary   (__xn_exec_shadow|__xn_exec_histage)
/* Short-hand for shadow syscall in Linux space. */
#define __xn_exec_secondary (__xn_exec_shadow|__xn_exec_lostage)

    u_long flags;

} xnsysent_t;

extern int nkgkptd;

#define xnshadow_ptd(t)    ((t)->ptd[nkgkptd])
#define xnshadow_thread(t) ((xnthread_t *)xnshadow_ptd(t))

#else /* !__KERNEL__ */

#include <sys/types.h>

#endif /* __KERNEL__ */

typedef struct xncompletion {

    long syncflag;		/* Semaphore variable. */
    pid_t pid;			/* Single waiter ID. */

} xncompletion_t;

#endif /* !_XENO_ASM_GENERIC_SYSCALL_H */

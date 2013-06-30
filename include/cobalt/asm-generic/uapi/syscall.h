/*
 * Copyright (C) 2013 Philippe Gerum <rpm@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */
#ifndef _COBALT_ASM_GENERIC_UAPI_SYSCALL_H
#define _COBALT_ASM_GENERIC_UAPI_SYSCALL_H

#include <asm/xenomai/uapi/features.h>
#include <asm/xenomai/uapi/syscall.h>

/* Xenomai multiplexer syscall. */
#define sc_nucleus_mux		555	/* Must fit within 15bit */
/* Xenomai nucleus syscalls. */
#define sc_nucleus_bind		 0	/* muxid = bind_to_interface(magic, &breq) */
#define sc_nucleus_migrate	 1	/* switched = xnshadow_relax/harden() */
#define sc_nucleus_info		 2	/* xnshadow_get_info(muxid,&info) */
#define sc_nucleus_arch		 3	/* r = xnarch_local_syscall(args) */
#define sc_nucleus_trace	 4	/* r = xntrace_xxx(...) */
#define sc_nucleus_heap_info	 5
#define sc_nucleus_current	 6	/* threadh = xnthread_handle(cur) */
#define sc_nucleus_current_info  7	/* r = xnshadow_current_info(&info) */
#define sc_nucleus_mayday        8	/* request mayday fixup */
#define sc_nucleus_backtrace     9	/* collect backtrace (relax tracing) */

struct xnbindreq {
	int feat_req;		/* Features userland requires. */
	int abi_rev;		/* ABI revision userland uses. */
	struct xnfeatinfo feat_ret; /* Features kernel space provides. */
};

#define XENOMAI_LINUX_DOMAIN  0
#define XENOMAI_XENO_DOMAIN   1

struct xnsysinfo {
	unsigned long long clockfreq;	/* Real-time clock frequency */
	unsigned long vdso;		/* Offset of nkvdso in the sem heap */
};

#define SIGSHADOW  SIGWINCH
#define SIGSHADOW_ACTION_HARDEN     1
#define SIGSHADOW_ACTION_BACKTRACE  2
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
#define SIGDEBUG_RESCNT_IMBALANCE	7

#define sigdebug_code(si)	((si)->si_value.sival_int)
#define sigdebug_reason(si)	(sigdebug_code(si) & 0xff)

#endif /* !_COBALT_ASM_GENERIC_UAPI_SYSCALL_H */

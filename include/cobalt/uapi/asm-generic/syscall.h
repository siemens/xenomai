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
#ifndef _COBALT_UAPI_ASM_GENERIC_SYSCALL_H
#define _COBALT_UAPI_ASM_GENERIC_SYSCALL_H

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
#define sc_nucleus_serialdbg     10	/* output to serial console (__ipipe_serial_debug()) */

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

#define SIGSHADOW_ACTION_HARDEN     1
#define SIGSHADOW_ACTION_BACKTRACE  2
#define SIGSHADOW_BACKTRACE_DEPTH   16

#endif /* !_COBALT_UAPI_ASM_GENERIC_SYSCALL_H */

/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _COBALT_X86_ASM_WRAPPERS_H
#define _COBALT_X86_ASM_WRAPPERS_H
#define _COBALT_X86_ASM_WRAPPERS_H

#ifndef __KERNEL__
#error "Pure kernel header included from user-space!"
#endif

#include <linux/version.h>
#include <asm-generic/xenomai/wrappers.h> /* Read the generic portion. */

#define __get_user_inatomic __get_user
#define __put_user_inatomic __put_user

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0)
#ifdef TS_USEDFPU

#define wrap_test_fpu_used(task)				\
	(task_thread_info(task)->status & TS_USEDFPU)

#define wrap_set_fpu_used(task)					\
	do {							\
		task_thread_info(task)->status |= TS_USEDFPU;	\
	} while(0)

#define wrap_clear_fpu_used(task)				\
	do {							\
		task_thread_info(task)->status &= ~TS_USEDFPU;	\
	} while(0)

#else /* !defined(TS_USEDFPU) */

#define wrap_test_fpu_used(task) ((task)->thread.has_fpu)
#define wrap_set_fpu_used(task)			\
	do {					\
		(task)->thread.has_fpu = 1;	\
	} while(0)
#define wrap_clear_fpu_used(task)		\
	do {					\
		(task)->thread.has_fpu = 0;	\
	} while(0)

#endif /* !defined(TS_USEDFPU) */
#else /* Linux >= 3.4.0 */
#include <asm/i387.h>
#include <asm/fpu-internal.h>

#define wrap_test_fpu_used(task) __thread_has_fpu(task)
#define wrap_set_fpu_used(task) __thread_set_has_fpu(task)
#define wrap_clear_fpu_used(task) __thread_clear_has_fpu(task)
#endif /* Linux >= 3.4.0 */

#define wrap_strncpy_from_user(dstP, srcP, n)		\
	strncpy_from_user_nocheck(dstP, srcP, n)

typedef union thread_xstate x86_fpustate;
#define x86_fpustate_ptr(t) ((t)->fpu.state)

#endif /* _COBALT_X86_ASM_WRAPPERS_H */

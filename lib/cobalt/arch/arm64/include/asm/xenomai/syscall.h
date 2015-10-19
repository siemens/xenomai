/*
 * Copyright (C) 2015 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _LIB_COBALT_ARM64_SYSCALL_H
#define _LIB_COBALT_ARM64_SYSCALL_H

#include <xeno_config.h>
#include <errno.h>
#include <cobalt/uapi/syscall.h>

#define __emit_asmdecl0							\
	register unsigned int __scno  __asm__ ("w8");			\
	register unsigned long __res  __asm__ ("x0")
#define __emit_asmdecl1							\
	__emit_asmdecl0; register unsigned long __x0  __asm__ ("x0")
#define __emit_asmdecl2							\
	__emit_asmdecl1; register unsigned long __x1  __asm__ ("x1")
#define __emit_asmdecl3							\
	__emit_asmdecl2; register unsigned long __x2  __asm__ ("x2")
#define __emit_asmdecl4							\
	__emit_asmdecl3; register unsigned long __x3  __asm__ ("x3")
#define __emit_asmdecl5							\
	__emit_asmdecl4; register unsigned long __x4  __asm__ ("x4")

#define __load_asminput0(__op)						\
	__scno = (unsigned int)__xn_syscode(__op)
#define __load_asminput1(__op, __a1)					\
	__load_asminput0(__op);						\
	__x0 = (unsigned long)(__a1)
#define __load_asminput2(__op, __a1, __a2)				\
	__load_asminput1(__op, __a1);					\
	__x1 = (unsigned long)(__a2)
#define __load_asminput3(__op, __a1, __a2, __a3)			\
	__load_asminput2(__op, __a1, __a2);				\
	__x2 = (unsigned long)(__a3)
#define __load_asminput4(__op, __a1, __a2, __a3, __a4)			\
	__load_asminput3(__op, __a1, __a2, __a3);			\
	__x3 = (unsigned long)(__a4)
#define __load_asminput5(__op, __a1, __a2, __a3, __a4, __a5)		\
	__load_asminput4(__op, __a1, __a2, __a3, __a4);			\
	__x4 = (unsigned long)(__a5)

#define __emit_syscall0(__args...)					\
	__asm__ __volatile__ (						\
		"svc 0;\n\t"						\
		: "=r" (__res)						\
		: "r" (__scno), ##__args				\
		: "cc", "memory");					\
	__res
#define __emit_syscall1(__a1, __args...)				\
	__emit_syscall0("r" (__x0),  ##__args)
#define __emit_syscall2(__a1, __a2, __args...)				\
	__emit_syscall1(__a1, "r" (__x1), ##__args)
#define __emit_syscall3(__a1, __a2, __a3, __args...)			\
	__emit_syscall2(__a1, __a2, "r" (__x2), ##__args)
#define __emit_syscall4(__a1, __a2, __a3, __a4, __args...)		\
	__emit_syscall3(__a1, __a2, __a3, "r" (__x3), ##__args)
#define __emit_syscall5(__a1, __a2, __a3, __a4, __a5, __args...)	\
	__emit_syscall4(__a1, __a2, __a3, __a4, "r" (__x4), ##__args)

#define XENOMAI_DO_SYSCALL(__argnr, __op, __args...)		\
	({							\
		__emit_asmdecl##__argnr;			\
		__load_asminput##__argnr(__op, ##__args);	\
		__emit_syscall##__argnr(__args);		\
	})

#define XENOMAI_SYSCALL0(__op)					\
	XENOMAI_DO_SYSCALL(0, __op)
#define XENOMAI_SYSCALL1(__op, __a1)				\
	XENOMAI_DO_SYSCALL(1, __op, __a1)
#define XENOMAI_SYSCALL2(__op, __a1, __a2)			\
	XENOMAI_DO_SYSCALL(2, __op, __a1, __a2)
#define XENOMAI_SYSCALL3(__op, __a1, __a2, __a3)		\
	XENOMAI_DO_SYSCALL(3, __op, __a1, __a2, __a3)
#define XENOMAI_SYSCALL4(__op, __a1, __a2, __a3, __a4)		\
	XENOMAI_DO_SYSCALL(4, __op, __a1, __a2, __a3, __a4)
#define XENOMAI_SYSCALL5(__op, __a1, __a2, __a3, __a4, __a5)	\
	XENOMAI_DO_SYSCALL(5, __op, __a1, __a2, __a3, __a4, __a5)
#define XENOMAI_SYSBIND(__breq)					\
	XENOMAI_DO_SYSCALL(1, sc_cobalt_bind, __breq)

#endif /* !_LIB_COBALT_ARM64_SYSCALL_H */

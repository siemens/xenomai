/*
 * Copyright (C) 2009 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _LIB_COBALT_NIOS2_SYSCALL_H
#define _LIB_COBALT_NIOS2_SYSCALL_H

#include <cobalt/uapi/syscall.h>
#include <errno.h>

#define __emit_syscall0(syscode)				\
	({							\
		long __ret;					\
								\
		__asm__ __volatile__ (				\
			"mov r2, %1\n\t"			\
			"trap\n\t"				\
			"mov %0, r2\n\t"			\
			: "=r"(__ret)				\
			: "r"(syscode)				\
			: "r2", "memory"			\
		);						\
		__ret;						\
	})

#define __emit_syscall1(syscode, a1)				\
	({							\
		long __ret;					\
								\
		__asm__ __volatile__ (				\
			"mov r2, %1\n\t"			\
			"mov r4, %2\n\t"			\
			"trap\n\t"				\
			"mov %0, r2\n\t"			\
			: "=r"(__ret)				\
			: "r"(syscode),				\
			  "r" ((long)a1)			\
			: "r2", "r4", "memory"			\
		);						\
		__ret;						\
	})

#define __emit_syscall2(syscode, a1, a2)			\
	({							\
		long __ret;					\
								\
		__asm__ __volatile__ (				\
			"mov r2, %1\n\t"			\
			"mov r4, %2\n\t"			\
			"mov r5, %3\n\t"			\
			"trap\n\t"				\
			"mov %0, r2\n\t"			\
			: "=r"(__ret)				\
			: "r"(syscode),				\
			  "r" ((long)a1),			\
			  "r" ((long)a2)			\
			: "r2", "r4", "r5", "memory"		\
		);						\
		__ret;						\
	})

#define __emit_syscall3(syscode, a1, a2, a3)			\
	({							\
		long __ret;					\
								\
		__asm__ __volatile__ (				\
			"mov r2, %1\n\t"			\
			"mov r4, %2\n\t"			\
			"mov r5, %3\n\t"			\
			"mov r6, %4\n\t"			\
			"trap\n\t"				\
			"mov %0, r2\n\t"			\
			: "=r"(__ret)				\
			: "r"(syscode),				\
			  "r" ((long)a1),			\
			  "r" ((long)a2),			\
			  "r" ((long)a3)			\
			: "r2", "r4", "r5", "r6", "memory"	\
		);						\
		__ret;						\
	})

#define __emit_syscall4(syscode, a1, a2, a3, a4)		\
	({							\
		long __ret;					\
								\
		__asm__ __volatile__ (				\
			"mov r2, %1\n\t"			\
			"mov r4, %2\n\t"			\
			"mov r5, %3\n\t"			\
			"mov r6, %4\n\t"			\
			"mov r7, %5\n\t"			\
			"trap\n\t"				\
			"mov %0, r2\n\t"			\
			: "=r"(__ret)				\
			: "r"(syscode),				\
			  "r" ((long)a1),			\
			  "r" ((long)a2),			\
			  "r" ((long)a3),			\
			  "r" ((long)a4)			\
			: "r2", "r4", "r5", "r6", "r7", "memory" \
		);						\
		__ret;						\
	})

#define __emit_syscall5(syscode, a1, a2, a3, a4, a5)		\
	({							\
		long __ret;					\
								\
		__asm__ __volatile__ (				\
			"mov r2, %1\n\t"			\
			"mov r4, %2\n\t"			\
			"mov r5, %3\n\t"			\
			"mov r6, %4\n\t"			\
			"mov r7, %5\n\t"			\
			"mov r8, %6\n\t"			\
			"trap\n\t"				\
			"mov %0, r2\n\t"			\
			: "=r"(__ret)				\
			: "r"(syscode),				\
			  "r" ((long)a1),			\
			  "r" ((long)a2),			\
			  "r" ((long)a3),			\
			  "r" ((long)a4),			\
			  "r" ((long)a5)			\
			: "r2", "r4", "r5", "r6", "r7", "r8", "memory" \
		);						\
		__ret;						\
	})

#define XENOMAI_DO_SYSCALL(nr, op, args...)		\
    __emit_syscall##nr(__xn_syscode(op), ##args)

#define XENOMAI_SYSCALL0(op)                XENOMAI_DO_SYSCALL(0,op)
#define XENOMAI_SYSCALL1(op,a1)             XENOMAI_DO_SYSCALL(1,op,a1)
#define XENOMAI_SYSCALL2(op,a1,a2)          XENOMAI_DO_SYSCALL(2,op,a1,a2)
#define XENOMAI_SYSCALL3(op,a1,a2,a3)       XENOMAI_DO_SYSCALL(3,op,a1,a2,a3)
#define XENOMAI_SYSCALL4(op,a1,a2,a3,a4)    XENOMAI_DO_SYSCALL(4,op,a1,a2,a3,a4)
#define XENOMAI_SYSCALL5(op,a1,a2,a3,a4,a5) XENOMAI_DO_SYSCALL(5,op,a1,a2,a3,a4,a5)
#define XENOMAI_SYSBIND(breq)		    XENOMAI_DO_SYSCALL(1,sc_cobalt_bind,breq)

#endif /* !_LIB_COBALT_NIOS2_SYSCALL_H */

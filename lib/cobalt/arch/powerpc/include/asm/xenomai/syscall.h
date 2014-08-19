/*
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
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

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#ifndef _LIB_COBALT_POWERPC_SYSCALL_H
#define _LIB_COBALT_POWERPC_SYSCALL_H

#include <cobalt/uapi/syscall.h>

/* Some code pulled from glibc's inline syscalls. */

#define LOADARGS_0(syscode, dummy...)			\
	__sc_0 = (unsigned long)(syscode)
#define LOADARGS_1(syscode, arg1)			\
	LOADARGS_0(syscode);				\
	__sc_3 = (unsigned long) (arg1)
#define LOADARGS_2(syscode, arg1, arg2)			\
	LOADARGS_1(syscode, arg1);			\
	__sc_4 = (unsigned long) (arg2)
#define LOADARGS_3(syscode, arg1, arg2, arg3)		\
	LOADARGS_2(syscode, arg1, arg2);		\
	__sc_5 = (unsigned long) (arg3)
#define LOADARGS_4(syscode, arg1, arg2, arg3, arg4)	\
	LOADARGS_3(syscode, arg1, arg2, arg3);		\
	__sc_6 = (unsigned long) (arg4)
#define LOADARGS_5(syscode, arg1, arg2, arg3, arg4, arg5) \
	LOADARGS_4(syscode, arg1, arg2, arg3, arg4);	\
	__sc_7 = (unsigned long) (arg5)

#define ASM_INPUT_0 "0" (__sc_0)
#define ASM_INPUT_1 ASM_INPUT_0, "1" (__sc_3)
#define ASM_INPUT_2 ASM_INPUT_1, "2" (__sc_4)
#define ASM_INPUT_3 ASM_INPUT_2, "3" (__sc_5)
#define ASM_INPUT_4 ASM_INPUT_3, "4" (__sc_6)
#define ASM_INPUT_5 ASM_INPUT_4, "5" (__sc_7)

#define XENOMAI_DO_SYSCALL(nr, op, args...)			\
  ({								\
	register unsigned long __sc_0  __asm__ ("r0");		\
	register unsigned long __sc_3  __asm__ ("r3");		\
	register unsigned long __sc_4  __asm__ ("r4");		\
	register unsigned long __sc_5  __asm__ ("r5");		\
	register unsigned long __sc_6  __asm__ ("r6");		\
	register unsigned long __sc_7  __asm__ ("r7");		\
								\
	LOADARGS_##nr(__xn_syscode(op), args);			\
	__asm__ __volatile__					\
		("sc           \n\t"				\
		 "mfcr %0      "				\
		: "=&r" (__sc_0),				\
		  "=&r" (__sc_3),  "=&r" (__sc_4),		\
		  "=&r" (__sc_5),  "=&r" (__sc_6),		\
		  "=&r" (__sc_7)				\
		: ASM_INPUT_##nr				\
		: "cr0", "ctr", "memory",			\
		  "r8", "r9", "r10","r11", "r12");		\
	(int)((__sc_0 & (1 << 28)) ? -__sc_3 : __sc_3);		\
  })

#define XENOMAI_SYSCALL0(op)                XENOMAI_DO_SYSCALL(0,op)
#define XENOMAI_SYSCALL1(op,a1)             XENOMAI_DO_SYSCALL(1,op,a1)
#define XENOMAI_SYSCALL2(op,a1,a2)          XENOMAI_DO_SYSCALL(2,op,a1,a2)
#define XENOMAI_SYSCALL3(op,a1,a2,a3)       XENOMAI_DO_SYSCALL(3,op,a1,a2,a3)
#define XENOMAI_SYSCALL4(op,a1,a2,a3,a4)    XENOMAI_DO_SYSCALL(4,op,a1,a2,a3,a4)
#define XENOMAI_SYSCALL5(op,a1,a2,a3,a4,a5) XENOMAI_DO_SYSCALL(5,op,a1,a2,a3,a4,a5)
#define XENOMAI_SYSBIND(breq)               XENOMAI_DO_SYSCALL(1,sc_cobalt_bind,breq)

#endif /* !_LIB_COBALT_POWERPC_SYSCALL_H */

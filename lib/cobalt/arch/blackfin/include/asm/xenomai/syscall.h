/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _LIB_COBALT_BLACKFIN_SYSCALL_H
#define _LIB_COBALT_BLACKFIN_SYSCALL_H

#include <cobalt/uapi/syscall.h>
#include <fcntl.h>
#include <errno.h>

#define __emit_syscall0(syscode, ...)					\
({									\
	long __res;							\
	__asm__ __volatile__ (						\
		"excpt 0;\n\t"						\
		: "=q0" (__res)						\
		: "qA"  (syscode),					\
		  ##__VA_ARGS__						\
		: "CC", "memory");					\
	__res;								\
})
#define __emit_syscall1(syscode, a1, ...)				\
	__emit_syscall0(syscode, "q0"(a1), ##__VA_ARGS__)
#define __emit_syscall2(syscode, a1, a2, ...)				\
	__emit_syscall1(syscode, a1, "q1"(a2), ##__VA_ARGS__)
#define __emit_syscall3(syscode, a1, a2, a3, ...)			\
	__emit_syscall2(syscode, a1, a2, "q2"(a3), ##__VA_ARGS__)
#define __emit_syscall4(syscode, a1, a2, a3, a4, ...)			\
	__emit_syscall3(syscode, a1, a2, a3, "q3"(a4), ##__VA_ARGS__)
#define __emit_syscall5(syscode, a1, a2, a3, a4, a5, ...)		\
	__emit_syscall4(syscode, a1, a2, a3, a4, "q4"(a5), ##__VA_ARGS__)

#define XENOMAI_DO_SYSCALL(nr, op, args...)		\
    __emit_syscall##nr(__xn_syscode(op), ##args)

#define XENOMAI_SYSCALL0(op)                XENOMAI_DO_SYSCALL(0,op)
#define XENOMAI_SYSCALL1(op,a1)             XENOMAI_DO_SYSCALL(1,op,a1)
#define XENOMAI_SYSCALL2(op,a1,a2)          XENOMAI_DO_SYSCALL(2,op,a1,a2)
#define XENOMAI_SYSCALL3(op,a1,a2,a3)       XENOMAI_DO_SYSCALL(3,op,a1,a2,a3)
#define XENOMAI_SYSCALL4(op,a1,a2,a3,a4)    XENOMAI_DO_SYSCALL(4,op,a1,a2,a3,a4)
#define XENOMAI_SYSCALL5(op,a1,a2,a3,a4,a5) XENOMAI_DO_SYSCALL(5,op,a1,a2,a3,a4,a5)
#define XENOMAI_SYSBIND(breq)		    XENOMAI_DO_SYSCALL(1,sc_cobalt_bind,breq)

#endif /* !_LIB_COBALT_BLACKFIN_SYSCALL_H */

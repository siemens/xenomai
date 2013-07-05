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

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */
#ifndef _COBALT_NIOS2_ASM_UAPI_ARITH_H
#define _COBALT_NIOS2_ASM_UAPI_ARITH_H

#include <asm/xenomai/uapi/features.h>

#define xnarch_add96and64(l0, l1, l2, s0, s1)				\
	do {								\
		__asm__ ("add %2, %2, %4\n\t"				\
			 "cmpltu r8, %2, %4\n\t"			\
			 "add %1, %1, %3\n\t"				\
			 "cmpltu r9, %1, %3\n\t"			\
			 "add %1, %1, r8\n\t"				\
			 "cmpltu r8, %1, r8\n\t"			\
			 "add r9, r9, r8\n\t"				\
			 "add %0, %0, r9\n\t"				\
			 : "=r"(l0), "=&r"(l1), "=&r"(l2)		\
			 : "r"(s0), "r"(s1), "0"(l0), "1"(l1), "2"(l2)  \
			 : "r8", "r9");					\
	} while (0);

#include <cobalt/uapi/asm-generic/arith.h>

#endif /* _COBALT_NIOS2_ASM_UAPI_ARITH_H */

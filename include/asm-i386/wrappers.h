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

#ifndef _XENO_ASM_I386_WRAPPERS_H
#define _XENO_ASM_I386_WRAPPERS_H

#ifndef __KERNEL__
#error "Pure kernel header included from user-space!"
#endif

#include <asm-generic/xenomai/wrappers.h> /* Read the generic portion. */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)

#define wrap_range_ok(task,addr,size) ({ \
	unsigned long flag,sum; \
	asm("addl %3,%1 ; sbbl %0,%0; cmpl %1,%4; sbbl $0,%0" \
		:"=&r" (flag), "=r" (sum) \
	        :"1" (addr),"g" ((int)(size)),"g" ((task)->addr_limit.seg)); \
	flag == 0; })

#else /*  LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)  */

#define wrap_range_ok(task,addr,size) ({ \
	unsigned long flag,sum; \
	asm("addl %3,%1 ; sbbl %0,%0; cmpl %1,%4; sbbl $0,%0" \
		:"=&r" (flag), "=r" (sum) \
	        :"1" (addr),"g" ((int)(size)),"g" ((task)->thread_info->addr_limit.seg)); \
	flag == 0; })

#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0) */

#endif /* _XENO_ASM_I386_WRAPPERS_H */

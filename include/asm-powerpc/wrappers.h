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

#ifndef _XENO_ASM_POWERPC_WRAPPERS_H
#define _XENO_ASM_POWERPC_WRAPPERS_H

#ifndef __KERNEL__
#error "Pure kernel header included from user-space!"
#endif

#include <asm-generic/xenomai/wrappers.h>	/* Read the generic portion. */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)

#define CONFIG_MMU 1

#define atomic_inc_and_test(v) (atomic_inc_return(v) == 0)
#define show_stack(p,sp)       print_backtrace(sp)	/* Only works for current. */

#define wrap_range_ok(task,addr,size) \
    (segment_eq((task)->thread.fs, KERNEL_DS) || __user_ok((unsigned long)(addr),(size)))

#else /*  LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)  */

#ifdef CONFIG_PPC64
#define wrap_range_ok(task,addr,size) \
    __access_ok(((__force unsigned long)(addr)),(size),(task->thread.fs))
#else
#define wrap_range_ok(task,addr,size) \
    ((unsigned long)(addr) <= (task)->thread.fs.seg			\
     && ((size) == 0 || (size) - 1 <= (task)->thread.fs.seg - (unsigned long)(addr)))
#endif

#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0) */

#endif /* _XENO_ASM_POWERPC_WRAPPERS_H */

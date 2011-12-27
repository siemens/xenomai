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

#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,0,0)
#error "Linux kernel 3.0 or above required for this architecture"
#endif

#define wrap_strncpy_from_user(dstP, srcP, n)	__strncpy_from_user(dstP, srcP, n)

#define wrap_phys_mem_prot(filp,pfn,size,prot) \
  phys_mem_access_prot(filp, pfn, size, prot)

#ifdef CONFIG_PPC64
#define wrap_range_ok(task,addr,size) \
    __access_ok(((__force unsigned long)(addr)),(size),(task->thread.fs))
#else /* !CONFIG_PPC64 */
#define wrap_range_ok(task,addr,size) \
    ((unsigned long)(addr) <= (task)->thread.fs.seg			\
     && ((size) == 0 || (size) - 1 <= (task)->thread.fs.seg - (unsigned long)(addr)))
#endif /* !CONFIG_PPC64 */

#include <asm-generic/xenomai/wrappers.h>	/* Read the generic portion. */

/* from linux/include/asm-powerpc/uaccess.h */
#define wrap_get_user(x, ptr)					\
({								\
	int __gu_size = sizeof(*(ptr));				\
	long __gu_err;						\
	unsigned long __gu_val;					\
	const __typeof__(*(ptr)) __user *__gu_addr = (ptr);	\
	__chk_user_ptr(ptr);					\
	__get_user_size(__gu_val, __gu_addr, gu_size, __gu_err);\
	(x) = (__typeof__(*(ptr)))__gu_val;			\
	__gu_err;						\
})

#define wrap_put_user(x, ptr)					\
({								\
	int __pu_size = sizeof(*(ptr));				\
	long __pu_err;						\
	__typeof__(*(ptr)) __user *__pu_addr = (ptr);		\
	__chk_user_ptr(ptr);					\
	__put_user_size((__typeof__(*(ptr)))(x),		\
			__pu_addr, __pu_size, __pu_err);	\
	__pu_err;						\
})

#define of_device platform_device
#define of_platform_driver platform_driver
#define of_register_platform_driver platform_driver_register
#define of_unregister_platform_driver platform_driver_unregister

#endif /* _XENO_ASM_POWERPC_WRAPPERS_H */

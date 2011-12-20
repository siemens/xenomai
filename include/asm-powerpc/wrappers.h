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
#include <linux/interrupt.h>

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

#define rthal_irq_desc_status(irq)	(rthal_irq_descp(irq)->status)
#define __ipipe_irq_handlerp(irq)		rthal_irq_descp(irq)->chip
typedef irq_handler_t rthal_irq_host_handler_t;

#if !defined(CONFIG_GENERIC_HARDIRQS) \
	|| LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37)
#define rthal_irq_chip_enable(irq)					\
	({								\
		int __err__ = 0;					\
		if (unlikely(__ipipe_irq_handlerp(irq)->unmask == NULL))	\
			__err__ = -ENODEV;				\
		else							\
			__ipipe_irq_handlerp(irq)->unmask(irq);		\
		__err__;						\
	})

#define rthal_irq_chip_disable(irq)					\
	({								\
		int __err__ = 0;					\
		if (__ipipe_irq_handlerp(irq)->mask == NULL)		\
			__err__ = -ENODEV;				\
		else							\
			__ipipe_irq_handlerp(irq)->mask(irq);		\
		__err__;						\
	})

#endif

#define rthal_irq_chip_end(irq)						\
	({ rthal_irq_descp(irq)->ipipe_end(irq, rthal_irq_descp(irq)); 0; })

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31)
#define mpc5xxx_get_bus_frequency(node)	mpc52xx_find_ipb_freq(node)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36)
#define of_device platform_device
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)
#define of_platform_driver platform_driver
#define of_register_platform_driver platform_driver_register
#define of_unregister_platform_driver platform_driver_unregister
#endif

#endif /* _XENO_ASM_POWERPC_WRAPPERS_H */

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

#ifndef _XENO_ASM_ARM_WRAPPERS_H
#define _XENO_ASM_ARM_WRAPPERS_H

#ifndef __KERNEL__
#error "Pure kernel header included from user-space!"
#endif

#include <asm-generic/xenomai/wrappers.h> /* Read the generic portion. */
#include <linux/interrupt.h>

#define wrap_range_ok(task,addr,size) ({ \
	unsigned long flag, sum; \
	__asm__("adds %1, %2, %3; sbcccs %1, %1, %0; movcc %0, #0" \
		: "=&r" (flag), "=&r" (sum) \
		: "r" (addr), "Ir" (size), "0" ((task)->thread_info->addr_limit) \
		: "cc"); \
	(flag == 0); })

typedef irqreturn_t (*rthal_irq_host_handler_t)(int irq,
						void *dev_id,
						struct pt_regs *regs);

#if IPIPE_MAJOR_NUMBER == 1 && /* There is no version 0. */ 	\
	(IPIPE_MINOR_NUMBER < 5 || IPIPE_PATCH_NUMBER < 3)
#define __ipipe_mach_release_timer()  \
	__ipipe_mach_set_dec(__ipipe_mach_ticks_per_jiffy)
#endif /* IPIPE < 1.5-03 */

#endif /* _XENO_ASM_ARM_WRAPPERS_H */

// vim: ts=4 et sw=4 sts=4

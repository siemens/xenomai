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

#ifndef _XENO_ASM_BLACKFIN_WRAPPERS_H
#define _XENO_ASM_BLACKFIN_WRAPPERS_H

#ifndef __KERNEL__
#error "Pure kernel header included from user-space!"
#endif

#include <asm-generic/xenomai/wrappers.h> /* Read the generic portion. */
#include <linux/interrupt.h>

#define wrap_access_ok(task,addr,size) _access_ok((unsigned long)(addr),(size))

typedef irqreturn_t (*rthal_irq_host_handler_t)(int irq,
						void *dev_id,
						struct pt_regs *regs);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)
#define rthal_irq_chip_enable(irq)					\
	({								\
		int __err__ = 0;					\
		if (rthal_irq_descp(irq)->chip->unmask == NULL)		\
			__err__ = -ENODEV;				\
		else {							\
			rthal_irq_descp(irq)->disable_depth = 0;	\
			rthal_irq_descp(irq)->chip->unmask(irq);	\
		}							\
		__err__;						\
	})
#define rthal_irq_chip_disable(irq)					\
	({								\
		int __err__ = 0;					\
		if (rthal_irq_descp(irq)->chip->mask == NULL)		\
			__err__ = -ENODEV;				\
		else {							\
			rthal_irq_descp(irq)->chip->mask(irq);		\
			rthal_irq_descp(irq)->disable_depth = 1;	\
		}							\
		__err__;						\
	})
#define rthal_irq_chip_end(irq)						\
	({								\
		int __err__ = 0;					\
		if (rthal_irq_descp(irq)->chip->unmask == NULL)		\
			__err__ = -ENODEV;				\
		else							\
			rthal_irq_descp(irq)->chip->unmask(irq);	\
		__err__;						\
	})
#else /* >= 2.6.19 */
#define rthal_irq_chip_enable(irq)   ({ rthal_irq_descp(irq)->chip->enable(irq); 0; })
#define rthal_irq_chip_disable(irq)  ({ rthal_irq_descp(irq)->chip->disable(irq); 0; })
#define rthal_irq_chip_end(irq)      ({ rthal_irq_descp(irq)->ipipe_end(irq, rthal_irq_descp(irq)); 0; })
#endif

#endif /* _XENO_ASM_BLACKFIN_WRAPPERS_H */

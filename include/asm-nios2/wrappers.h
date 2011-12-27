/*
 * Copyright (C) 2009 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _XENO_ASM_NIOS2_WRAPPERS_H
#define _XENO_ASM_NIOS2_WRAPPERS_H

#ifndef __KERNEL__
#error "Pure kernel header included from user-space!"
#endif

#include <asm-generic/xenomai/wrappers.h> /* Read the generic portion. */

#define wrap_phys_mem_prot(filp, pfn, size, prot)  (prot)

#define wrap_strncpy_from_user(dstP, srcP, n)	strncpy_from_user(dstP, srcP, n)

#define PAGE_SHARED  __pgprot(0)

#ifdef CONFIG_XENO_LEGACY_IPIPE

#if !defined(CONFIG_GENERIC_HARDIRQS) \
	|| LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37)
#define ipipe_enable_irq(irq)   irq_to_desc(irq)->chip->enable(irq)
#define ipipe_disable_irq(irq)  irq_to_desc(irq)->chip->disable(irq)
#endif

#endif /* CONFIG_XENO_LEGACY_IPIPE */

#endif /* _XENO_ASM_NIOS2_WRAPPERS_H */

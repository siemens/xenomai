/*
 * Copyright (C) 2006 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
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
#ifndef _COBALT_ASM_SH_FPTEST_H
#define _COBALT_ASM_SH_FPTEST_H

#ifdef __KERNEL__
#include <linux/module.h>

static inline int fp_kernel_supported(void)
{
	return 0;
}

static inline int fp_linux_begin(void)
{
	return -ENOSYS;
}

static inline void fp_linux_end(void)
{
}

#else /* !__KERNEL__ */
#include <stdio.h>
#define printk printf
#endif /* !__KERNEL__ */

static inline void fp_features_init(void)
{
}

static inline void fp_regs_set(unsigned val)
{
}

static inline unsigned fp_regs_check(unsigned val)
{
	return val;
}

#endif /* _COBALT_ASM_SH_FPTEST_H */

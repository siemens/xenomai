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

#include <linux/version.h>
#include <asm-generic/xenomai/wrappers.h> /* Read the generic portion. */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0)
#define wrap_strncpy_from_user(dstP, srcP, n)	__strncpy_from_user(dstP, srcP, n)
#else
#define wrap_strncpy_from_user(dstP, srcP, n)	strncpy_from_user(dstP, srcP, n)
#endif

#define __put_user_inatomic __put_user
#define __get_user_inatomic __get_user

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 1, 0) && !defined(CONFIG_VFP_3_2_BACKPORT)
#define vfp_current_hw_state last_VFP_context
#endif /* Linux < 3.1 */

static inline void fp_init(union fp_state *state)
{
	/* FIXME: This is insufficient. */
	memset(state, 0, sizeof(*state));
}

#endif /* _XENO_ASM_ARM_WRAPPERS_H */

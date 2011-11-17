/*
 * Copyright (C) 2011 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _XENO_ASM_SH_ATOMIC_H
#define _XENO_ASM_SH_ATOMIC_H

#ifdef __KERNEL__
#include <asm/atomic.h>

#define xnarch_atomic_set_mask(pflags,mask) \
	atomic_set_mask(mask, (atomic_t *)(pflags))
#define xnarch_atomic_clear_mask(pflags,mask) \
	atomic_clear_mask(mask, (atomic_t *)(pflags))

#else /* !__KERNEL */
#include <endian.h>

static inline unsigned long long load_u64(volatile void *p)
{
	union {
#if __BYTE_ORDER == __BIG_ENDIAN
		struct {
			unsigned long high;
			unsigned long low;
		} e;
#else /* __LITTLE_ENDIAN */
		struct {
			unsigned long low;
			unsigned long high;
		} e;
#endif /* __LITTLE_ENDIAN */
		struct {
			unsigned long l1;
			unsigned long l2;
		} v;
	} u;

	__asm__ __volatile__ (
		"   .align 2              \n\t"
		"   mova    1f,   r0      \n\t" /* r0 = end point */
		"   nop                   \n\t"
		"   mov    r15,   r1      \n\t" /* r1 = saved sp */
		"   mov    #-4,   r15     \n\t" /* LOGIN */
		"   mov.l  @%2,   %0      \n\t" /* load first 32bit word */
		"   mov.l  @(4, %2),   %1 \n\t" /* load second 32bit word */
		"1: mov     r1,   r15     \n\t" /* LOGOUT */
		: "=&r" (u.v.l1), "=&r" (u.v.l2)
		: "r" (p)
		: "memory", "r0", "r1");

	return ((unsigned long long)u.e.high << 32) | u.e.low;
}

#endif /* !__KERNEL__ */

#include <asm-generic/xenomai/atomic.h>

#endif /* !_XENO_ASM_SH_ATOMIC_H */

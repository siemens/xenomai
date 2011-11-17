/*
 * Copyright (C) 2003,2004 Philippe Gerum <rpm@xenomai.org>.
 *
 * 64-bit PowerPC adoption
 *   copyright (C) 2005 Taneli Vähäkangas and Heikki Lindholm
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

#ifndef _XENO_ASM_POWERPC_ATOMIC_H
#define _XENO_ASM_POWERPC_ATOMIC_H

#ifdef __KERNEL__

#ifdef CONFIG_PPC64
static __inline__ void atomic64_clear_mask(unsigned long mask,
					   unsigned long *ptr)
{
    __asm__ __volatile__ ("\n\
1:	ldarx	5,0,%0 \n\
	andc	5,5,%1\n"
"	stdcx.	5,0,%0 \n\
	bne-	1b"
	: /*no output*/
	: "r" (ptr), "r" (mask)
	: "r5", "cc", "memory");
}
#define xnarch_atomic_clear_mask(pflags,mask)  atomic64_clear_mask(mask,pflags)

static __inline__ void atomic64_set_mask(unsigned long mask,
					 unsigned long *ptr)
{
    __asm__ __volatile__ ("\n\
1:	ldarx	5,0,%0 \n\
	or	5,5,%1\n"
"	stdcx.	5,0,%0 \n\
	bne-	1b"
	: /*no output*/
	: "r" (ptr), "r" (mask)
	: "r5", "cc", "memory");
}
#define xnarch_atomic_set_mask(pflags,mask)    atomic64_set_mask(mask,pflags)

#else /* !CONFIG_PPC64 */
 /* These are defined in arch/{ppc,powerpc}/kernel/misc[_32].S on 32-bit PowerPC */
void atomic_set_mask(unsigned long mask, unsigned long *ptr);
void atomic_clear_mask(unsigned long mask, unsigned long *ptr);

#endif /* !CONFIG_PPC64 */

#endif /* __KERNEL__ */

#include <asm-generic/xenomai/atomic.h>

#endif /* !_XENO_ASM_POWERPC_ATOMIC_H */

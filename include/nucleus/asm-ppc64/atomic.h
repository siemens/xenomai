/*
 * Xenomai 64-bit PowerPC adoption
 * Copyright (C) 2005 Taneli Vähäkangas and Heikki Lindholm
 * based on previous work:
 *     
 * Copyright (C) 2003,2004 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _XENO_ASM_PPC64_ATOMIC_H
#define _XENO_ASM_PPC64_ATOMIC_H

#include <asm/atomic.h>

static __inline__ void atomic_set_mask(unsigned long mask,
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

static __inline__ void atomic_clear_mask(unsigned long mask,
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

#ifdef __KERNEL__

#include <linux/bitops.h>
#include <asm/system.h>

#define xnarch_atomic_xchg(ptr,v)       xchg(ptr,v)
#define xnarch_memory_barrier()  	smp_mb()

void atomic_set_mask(unsigned long mask, /* from arch/ppc/kernel/misc.S */
		     unsigned long *ptr);

#define xnarch_atomic_set(pcounter,i)          atomic_set(pcounter,i)
#define xnarch_atomic_get(pcounter)            atomic_read(pcounter)
#define xnarch_atomic_inc(pcounter)            atomic_inc(pcounter)
#define xnarch_atomic_dec(pcounter)            atomic_dec(pcounter)
#define xnarch_atomic_inc_and_test(pcounter)   atomic_inc_and_test(pcounter)
#define xnarch_atomic_dec_and_test(pcounter)   atomic_dec_and_test(pcounter)
#define xnarch_atomic_set_mask(pflags,mask)    atomic_set_mask(mask,pflags)
#define xnarch_atomic_clear_mask(pflags,mask)  atomic_clear_mask(mask,pflags)

#else /* !__KERNEL__ */

#include <linux/config.h>

/* from asm-ppc64/memory.h */
/*
 * Arguably the bitops and *xchg operations don't imply any memory barrier
 * or SMP ordering, but in fact a lot of drivers expect them to imply
 * both, since they do on x86 cpus.
 */
#ifdef CONFIG_SMP
#define EIEIO_ON_SMP	"eieio\n"
#define ISYNC_ON_SMP	"\n\tisync"
#else
#define EIEIO_ON_SMP
#define ISYNC_ON_SMP
#endif

/*
 * from <linux/asm-ppc64/system.h> and <linux/asm-ppc64/atomic.h>
 */

static __inline__ unsigned long
xnarch_atomic_xchg(volatile unsigned long *m, unsigned long val)
{
	unsigned long dummy;

	__asm__ __volatile__(
	EIEIO_ON_SMP
"1:	ldarx %0,0,%3		# __xchg_u64\n\
	stdcx. %2,0,%3\n\
2:	bne- 1b"
	ISYNC_ON_SMP
	: "=&r" (dummy), "=m" (*m)
	: "r" (val), "r" (m)
	: "cc", "memory");

	return (dummy);
}

#define xnarch_memory_barrier() __asm__ __volatile__ ("sync" : : : "memory")

#define xnarch_atomic_set(pcounter,i)          (((pcounter)->counter) = (i))
#define xnarch_atomic_get(pcounter)            ((pcounter)->counter)
#define xnarch_atomic_inc(pcounter)            atomic_inc(pcounter)
#define xnarch_atomic_dec(pcounter)            atomic_dec(pcounter)
#define xnarch_atomic_inc_and_test(pcounter)   (atomic_inc_return(pcounter) == 0)
#define xnarch_atomic_dec_and_test(pcounter)   (atomic_dec_return(pcounter) == 0)
#define xnarch_atomic_set_mask(pflags,mask)    atomic_set_mask(mask,pflags)
#define xnarch_atomic_clear_mask(pflags,mask)  atomic_clear_mask(mask,pflags)

#define cpu_relax()  xnarch_memory_barrier()

#endif /* __KERNEL__ */

typedef atomic_t atomic_counter_t;
typedef unsigned long atomic_flags_t;

#endif /* !_XENO_ASM_PPC64_ATOMIC_H */

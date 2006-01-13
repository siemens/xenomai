/*
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

#ifndef _XENO_ASM_PPC_ATOMIC_H
#define _XENO_ASM_PPC_ATOMIC_H

#include <asm/atomic.h>

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

#ifdef CONFIG_IBM405_ERR77
#define PPC405_ERR77(ra,rb)	"dcbt " #ra "," #rb ";"
#else
#define PPC405_ERR77(ra,rb)
#endif

/*
 * Shamelessly lifted from <linux/asm-ppc/system.h>
 * and <linux/asm-ppc/atomic.h>
 */

static inline unsigned long xnarch_atomic_xchg (volatile void *ptr,
						unsigned long x)
{
    unsigned long prev;

    __asm__ __volatile__ ("\n\
1:	lwarx	%0,0,%2 \n"
	PPC405_ERR77(0,%2) \
"	stwcx.	%3,0,%2 \n\
	bne-	1b"
	: "=&r" (prev), "=m" (*(volatile unsigned long *)ptr)
	: "r" (ptr), "r" (x), "m" (*(volatile unsigned long *)ptr)
	: "cc", "memory");

    return prev;
}

#ifdef CONFIG_SMP
#define SMP_SYNC  "sync"
#define SMP_ISYNC "\n\tisync"
#else /* !CONFIG_SMP */
#define SMP_SYNC  ""
#define SMP_ISYNC
#endif /* CONFIG_SMP */

static __inline__ void atomic_inc(atomic_t *v)

{
    int t;

    __asm__ __volatile__(
"1:	lwarx	%0,0,%2\n\
	addic	%0,%0,1\n"
	PPC405_ERR77(0,%2)
"	stwcx.	%0,0,%2 \n\
	bne-	1b"
	: "=&r" (t), "=m" (v->counter)
	: "r" (&v->counter), "m" (v->counter)
	: "cc");
}

static __inline__ int atomic_inc_return(atomic_t *v)

{
    int t;

    __asm__ __volatile__(
"1:	lwarx	%0,0,%1\n\
	addic	%0,%0,1\n"
	PPC405_ERR77(0,%1)
"	stwcx.	%0,0,%1 \n\
	bne-	1b"
	SMP_ISYNC
	: "=&r" (t)
	: "r" (&v->counter)
	: "cc", "memory");

    return t;
}

static __inline__ void atomic_dec(atomic_t *v)

{
    int t;

    __asm__ __volatile__(
"1:	lwarx	%0,0,%2\n\
	addic	%0,%0,-1\n"
	PPC405_ERR77(0,%2)\
"	stwcx.	%0,0,%2\n\
	bne-	1b"
	: "=&r" (t), "=m" (v->counter)
	: "r" (&v->counter), "m" (v->counter)
	: "cc");
}

static __inline__ int atomic_dec_return(atomic_t *v)

{
    int t;

    __asm__ __volatile__(
"1:	lwarx	%0,0,%1\n\
	addic	%0,%0,-1\n"
	PPC405_ERR77(0,%1)
"	stwcx.	%0,0,%1\n\
	bne-	1b"
	SMP_ISYNC
	: "=&r" (t)
	: "r" (&v->counter)
	: "cc", "memory");

    return t;
}

static __inline__ void atomic_set_mask(unsigned long mask,
				       unsigned long *ptr)
{
    __asm__ __volatile__ ("\n\
1:	lwarx	5,0,%0 \n\
	or	5,5,%1\n"
	PPC405_ERR77(0,%0) \
"	stwcx.	5,0,%0 \n\
	bne-	1b"
	: /*no output*/
	: "r" (ptr), "r" (mask)
	: "r5", "cc", "memory");
}

static __inline__ void atomic_clear_mask(unsigned long mask,
					 unsigned long *ptr)
{
    __asm__ __volatile__ ("\n\
1:	lwarx	5,0,%0 \n\
	andc	5,5,%1\n"
	PPC405_ERR77(0,%0) \
"	stwcx.	5,0,%0 \n\
	bne-	1b"
	: /*no output*/
	: "r" (ptr), "r" (mask)
	: "r5", "cc", "memory");
}

#define xnarch_memory_barrier()  __asm__ __volatile__("": : :"memory")

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

#endif /* !_XENO_ASM_PPC_ATOMIC_H */

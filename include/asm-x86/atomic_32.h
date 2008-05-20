/*
 * Copyright (C) 2003 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _XENO_ASM_X86_ATOMIC_32_H
#define _XENO_ASM_X86_ATOMIC_32_H
#define _XENO_ASM_X86_ATOMIC_H

#define XNARCH_HAVE_US_ATOMIC_CMPXCHG

#ifdef __KERNEL__

#include <linux/bitops.h>
#include <asm/atomic.h>
#include <asm/system.h>

#define xnarch_atomic_set(pcounter,i)          atomic_set(pcounter,i)
#define xnarch_atomic_get(pcounter)            atomic_read(pcounter)
#define xnarch_atomic_inc(pcounter)            atomic_inc(pcounter)
#define xnarch_atomic_dec(pcounter)            atomic_dec(pcounter)
#define xnarch_atomic_inc_and_test(pcounter)   atomic_inc_and_test(pcounter)
#define xnarch_atomic_dec_and_test(pcounter)   atomic_dec_and_test(pcounter)
#define xnarch_atomic_set_mask(pflags,mask)    atomic_set_mask(mask,pflags)
#define xnarch_atomic_clear_mask(pflags,mask)  atomic_clear_mask(mask,pflags)
#define xnarch_atomic_xchg(ptr,x)              xchg(ptr,x)
#define xnarch_atomic_cmpxchg(pcounter,old,new) \
	atomic_cmpxchg((pcounter),(old),(new))

#define xnarch_memory_barrier()  smp_mb()

typedef atomic_t atomic_counter_t;
typedef atomic_t xnarch_atomic_t;

#include <asm-generic/xenomai/atomic.h>

#else /* !__KERNEL__ */

#ifdef CONFIG_SMP
#define LOCK_PREFIX "lock ; "
#else
#define LOCK_PREFIX ""
#endif

typedef struct { int counter; } xnarch_atomic_t;

struct __xeno_xchg_dummy { unsigned long a[100]; };
#define __xeno_xg(x) ((struct __xeno_xchg_dummy *)(x))

#define xnarch_atomic_get(v)		((v)->counter)

#define xnarch_atomic_set(v,i)		(((v)->counter) = (i))

static inline unsigned long xnarch_atomic_xchg (volatile void *ptr,
						unsigned long x)
{
	__asm__ __volatile__(LOCK_PREFIX "xchgl %0,%1"
			     :"=r" (x)
			     :"m" (*__xeno_xg(ptr)), "0" (x)
			     :"memory");
	return x;
}

static inline unsigned long
xnarch_atomic_cmpxchg(xnarch_atomic_t *v, unsigned long old, unsigned long new)
{
	volatile void *ptr = &v->counter;
	unsigned long prev;

	__asm__ __volatile__(LOCK_PREFIX "cmpxchgl %1,%2"
			     : "=a"(prev)
			     : "r"(new), "m"(*__xeno_xg(ptr)), "0"(old)
			     : "memory");
	return prev;
}

#define xnarch_memory_barrier()  __asm__ __volatile__("": : :"memory")

#define xnarch_read_memory_barrier() \
	__asm__ __volatile__ (LOCK_PREFIX "addl $0,0(%%esp)": : :"memory")
#define xnarch_write_memory_barrier()	xnarch_memory_barrier()

#endif /* __KERNEL__ */

typedef unsigned long atomic_flags_t;

#include <asm-generic/xenomai/atomic.h>

#endif /* !_XENO_ASM_X86_ATOMIC_32_H */

/*
 * Copyright (C) 2007 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _XENO_ASM_X86_ATOMIC_H
#define _XENO_ASM_X86_ATOMIC_H

#include <asm/xenomai/features.h>

typedef unsigned long atomic_flags_t;

#ifdef __KERNEL__

#include <linux/bitops.h>
#include <asm/atomic.h>
#include <asm/system.h>
#include <asm/xenomai/wrappers.h>

#define xnarch_atomic_set(pcounter,i)	atomic_long_set(pcounter,i)
#define xnarch_atomic_get(pcounter)	atomic_long_read(pcounter)
#define xnarch_atomic_inc(pcounter)	atomic_long_inc(pcounter)
#define xnarch_atomic_dec(pcounter)	atomic_long_dec(pcounter)
#define xnarch_atomic_inc_and_test(pcounter) \
	atomic_long_inc_and_test(pcounter)
#define xnarch_atomic_dec_and_test(pcounter) \
	atomic_long_dec_and_test(pcounter)
#define xnarch_atomic_cmpxchg(pcounter,old,new) \
	atomic_long_cmpxchg((pcounter),(old),(new))

typedef atomic_long_t atomic_counter_t;
typedef atomic_long_t xnarch_atomic_t;

#define xnarch_atomic_set_mask(pflags,mask) \
	atomic_set_mask((mask),(unsigned *)(pflags))
#define xnarch_atomic_clear_mask(pflags,mask) \
	atomic_clear_mask((mask),(unsigned *)(pflags))
#define xnarch_atomic_xchg(ptr,x)	xchg(ptr,x)

#define xnarch_memory_barrier()		smp_mb()
#define xnarch_read_memory_barrier()    rmb()
#define xnarch_write_memory_barrier()   wmb()

#else /* !__KERNEL__ */

#include <xeno_config.h>

#ifdef CONFIG_SMP
#define LOCK_PREFIX "lock ; "
#else
#define LOCK_PREFIX ""
#endif

typedef struct { unsigned long counter; } xnarch_atomic_t;

#define xnarch_atomic_get(v)		((v)->counter)

#define xnarch_atomic_set(v,i)		(((v)->counter) = (i))

static inline void cpu_relax(void)
{
	asm volatile("rep; nop" ::: "memory");
}

#ifdef __i386__

struct __xeno_xchg_dummy { unsigned long a[100]; };
#define __xeno_xg(x) ((struct __xeno_xchg_dummy *)(x))

static inline unsigned long xnarch_atomic_xchg (volatile void *ptr,
						unsigned long x)
{
	__asm__ __volatile__("xchgl %0,%1"
			     :"=r" (x)
			     :"m" (*__xeno_xg(ptr)), "0" (x)
			     :"memory");
	return x;
}

static inline unsigned long
xnarch_atomic_cmpxchg(xnarch_atomic_t *v, unsigned long old, unsigned long newval)
{
	volatile void *ptr = &v->counter;
	unsigned long prev;

	__asm__ __volatile__(LOCK_PREFIX "cmpxchgl %1,%2"
			     : "=a"(prev)
			     : "r"(newval), "m"(*__xeno_xg(ptr)), "0"(old)
			     : "memory");
	return prev;
}

#define xnarch_memory_barrier()		__asm__ __volatile__("": : :"memory")
#define xnarch_read_memory_barrier() \
	__asm__ __volatile__ (LOCK_PREFIX "addl $0,0(%%esp)": : :"memory")
#define xnarch_write_memory_barrier() \
	__asm__ __volatile__ (LOCK_PREFIX "addl $0,0(%%esp)": : :"memory")

#else /* x86_64 */

#define __xeno_xg(x) ((volatile long *)(x))

static inline unsigned long xnarch_atomic_xchg (volatile void *ptr,
						unsigned long x)
{
	__asm__ __volatile__("xchgq %0,%1"
			     :"=r" (x)
			     :"m" (*__xeno_xg(ptr)), "0" (x)
			     :"memory");
	return x;
}

static inline unsigned long
xnarch_atomic_cmpxchg(xnarch_atomic_t *v, unsigned long old, unsigned long newval)
{
	volatile void *ptr = &v->counter;
	unsigned long prev;

	__asm__ __volatile__(LOCK_PREFIX "cmpxchgq %1,%2"
			     : "=a"(prev)
			     : "r"(newval), "m"(*__xeno_xg(ptr)), "0"(old)
			     : "memory");
	return prev;
}

#define xnarch_memory_barrier()		asm volatile("mfence":::"memory")
#define xnarch_read_memory_barrier()	asm volatile("lfence":::"memory")
#define xnarch_write_memory_barrier()	asm volatile("sfence":::"memory")

#endif /* x86_64 */

#endif /* __KERNEL__ */

#endif /* !_XENO_ASM_X86_ATOMIC_64_H */

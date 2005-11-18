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

#ifndef _XENO_ASM_BFINNOMMU_ATOMIC_H
#define _XENO_ASM_BFINNOMMU_ATOMIC_H

#ifdef __KERNEL__

#include <linux/bitops.h>
#include <asm/atomic.h>
#include <asm/system.h>

#define atomic_xchg(ptr,v)       xchg(ptr,v)
#define xnarch_memory_barrier()  smp_mb()

#define xnarch_atomic_set(pcounter,i)          atomic_set(pcounter,i)
#define xnarch_atomic_get(pcounter)            atomic_read(pcounter)
#define xnarch_atomic_inc(pcounter)            atomic_inc(pcounter)
#define xnarch_atomic_dec(pcounter)            atomic_dec(pcounter)
#define xnarch_atomic_inc_and_test(pcounter)   atomic_inc_and_test(pcounter)
#define xnarch_atomic_dec_and_test(pcounter)   atomic_dec_and_test(pcounter)
#define xnarch_atomic_set_mask(pflags,mask)    atomic_set_mask(mask,pflags)
#define xnarch_atomic_clear_mask(pflags,mask)  atomic_clear_mask(mask,pflags)

typedef atomic_t atomic_counter_t;

#else /* !__KERNEL__ */

typedef struct { volatile int counter; } atomic_counter_t;

/*
 * Atomic xchg lifted from linux/include/asm-bfinnommu/system.h 
 */

struct __xchg_dummy { unsigned long a[100]; };
#define __xg(x) ((volatile struct __xchg_dummy *)(x))

static inline unsigned long __xchg(unsigned long x, volatile void * ptr, int size)
{
  unsigned long tmp=0;
  unsigned long flags = 0;

  local_irq_save_hw(flags);

  switch (size) {
  case 1:
    __asm__ __volatile__
    ("%0 = b%2 (z);\n\t"
     "b%2 = %1;\n\t"
    : "=&d" (tmp) : "d" (x), "m" (*__xg(ptr)) : "memory");
    break;
  case 2:
    __asm__ __volatile__
    ("%0 = w%2 (z);\n\t"
     "w%2 = %1;\n\t"
    : "=&d" (tmp) : "d" (x), "m" (*__xg(ptr)) : "memory");
    break;
  case 4:
    __asm__ __volatile__
    ("%0 = %2;\n\t"
     "%2 = %1;\n\t"
    : "=&d" (tmp) : "d" (x), "m" (*__xg(ptr)) : "memory");
    break;
  }
  local_irq_restore_hw(flags);
  return tmp;
}

#define atomic_xchg(ptr,x) \
((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))

/*
 * Atomic operations lifted from linux/include/asm-bfinnommu/atomic.h 
 */

static __inline__ void atomic_inc(atomic_counter_t *v)
{
	int __temp = 0;
	__asm__ __volatile__(
		"cli R3;\n\t"
		"%0 = %1;\n\t"
		"%0 += 1;\n\t"
		"%1 = %0;\n\t"
		"sti R3;\n\t"
		: "=d" (__temp), "=m" (v->counter)
		: "m" (v->counter), "0" (__temp)
		: "R3");
}

static __inline__ int atomic_dec_return(int i, atomic_counter_t *v)
{
	int __temp = 0;
	__asm__ __volatile__(
		"cli R3;\n\t"
		"%0 = %1;\n\t"
		"%0 = %0 - %2;\n\t"
		"%1 = %0;\n\t"
		"sti R3;\n\t"
		: "=d" (__temp), "=m" (v->counter)
		: "d" (i), "m" (v->counter), "0" (__temp)
		: "R3");

	return __temp;
}

static __inline__ void atomic_set_mask(unsigned long mask,
				       unsigned long *ptr)
{
	int __temp = 0;
        __asm__ __volatile__(
		"cli R3;\n\t"
		"%0 = %1;\n\t"
		"%0 = %0 | %2;\n\t"
		"%1 = %0;\n\t"
		"sti R3;\n\t"
		: "=d" (__temp), "=m" (v->counter)
		: "d" (mask), "m" (v->counter), "0" (__temp)
		: "R3");
}

static __inline__ void atomic_clear_mask(unsigned long mask,
					 unsigned long *ptr)
{
	int __temp = 0;
        __asm__ __volatile__(
		"cli R3;\n\t"
		"%0 = %1;\n\t"
		"%0 = %0 & %2;\n\t"
		"%1 = %0;\n\t"
		"sti R3;\n\t"
		: "=d" (__temp), "=m" (v->counter)
		: "d" (~(mask)), "m" (v->counter), "0" (__temp)
		: "R3");
}

#define xnarch_atomic_xchg(ptr,v)   atomic_xchg(ptr,v)
#define xnarch_memory_barrier()     __asm__ __volatile__("": : :"memory")

#define xnarch_atomic_inc(pcounter)            atomic_inc(pcounter)
#define xnarch_atomic_dec_and_test(pcounter)   (atomic_sub_return(1,pcounter) == 0)
#define xnarch_atomic_set_mask(pflags,mask)    atomic_set_mask(mask,pflags)
#define xnarch_atomic_clear_mask(pflags,mask)  atomic_clear_mask(mask,pflags)

#define cpu_relax()  xnarch_memory_barrier()

#endif /* __KERNEL__ */

typedef unsigned long atomic_flags_t;

#endif /* !_XENO_ASM_BFINNOMMU_ATOMIC_H */

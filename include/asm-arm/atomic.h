/*
 * Copyright (C) 2003,2004 Philippe Gerum <rpm@xenomai.org>.
 *
 * ARM port
 *   Copyright (C) 2005 Stelian Pop
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

#ifndef _XENO_ASM_ARM_ATOMIC_H
#define _XENO_ASM_ARM_ATOMIC_H

#ifdef __KERNEL__

#include <linux/bitops.h>
#include <asm/atomic.h>
#include <asm/system.h>
#include <asm/xenomai/features.h>

typedef atomic_t atomic_counter_t;
typedef atomic_t xnarch_atomic_t;

#define xnarch_atomic_xchg(ptr,v)       xchg(ptr,v)
#define xnarch_memory_barrier()  	smp_mb()

#if __LINUX_ARM_ARCH__ >= 6

static inline void
xnarch_atomic_set_mask(unsigned long *addr, unsigned long mask)
{
    unsigned long tmp, tmp2;

    __asm__ __volatile__("@ atomic_set_mask\n"
"1: ldrex   %0, [%2]\n"
"   orr     %0, %0, %3\n"
"   strex   %1, %0, [%2]\n"
"   teq     %1, #0\n"
"   bne     1b"
    : "=&r" (tmp), "=&r" (tmp2)
    : "r" (addr), "Ir" (mask)
    : "cc");
}
#else /* ARM_ARCH_6 */
static inline void
xnarch_atomic_set_mask(unsigned long *addr, unsigned long mask)
{
    unsigned long flags;

    local_irq_save_hw(flags);
    *addr |= mask;
    local_irq_restore_hw(flags);
}

#endif /* ARM_ARCH_6 */

#define xnarch_atomic_set(pcounter,i)          atomic_set((pcounter),i)
#define xnarch_atomic_get(pcounter)            atomic_read(pcounter)
#define xnarch_atomic_inc(pcounter)            atomic_inc(pcounter)
#define xnarch_atomic_dec(pcounter)            atomic_dec(pcounter)
#define xnarch_atomic_clear_mask(addr, mask)   atomic_clear_mask((mask), (addr))
#define xnarch_atomic_cmpxchg(pcounter, old, new) \
	atomic_cmpxchg((pcounter), (old), (new))
#define xnarch_atomic_inc_and_test(pcounter)   atomic_inc_and_test(pcounter)
#define xnarch_atomic_dec_and_test(pcounter)   atomic_dec_and_test(pcounter)

#else /* !__KERNEL__ */

#include <asm/xenomai/features.h>
#include <asm/xenomai/syscall.h>
#include <nucleus/compiler.h>

typedef struct { volatile int counter; } xnarch_atomic_t;

#define xnarch_atomic_get(v)	((v)->counter)

/*
 * This function doesn't exist, so you'll get a linker error
 * if something tries to do an invalid xchg().
 */
extern void __xnarch_xchg_called_with_bad_pointer(void);

static __inline__ unsigned long
__xchg(volatile void *ptr, unsigned long x, unsigned int size)
{
    unsigned long ret;
#if CONFIG_XENO_ARM_ARCH >= 6
    unsigned int tmp;
#endif

    if (size != 4) {
        __xnarch_xchg_called_with_bad_pointer();
        return 0;
    }

#if CONFIG_XENO_ARM_ARCH >= 6
    asm volatile("@ __xchg4\n"
"1: ldrex   %0, [%3]\n"
"   strex   %1, %2, [%3]\n"
"   teq     %1, #0\n"
"   bne     1b"
    : "=&r" (ret), "=&r" (tmp)
    : "r" (x), "r" (ptr)
    : "memory", "cc");
#elif defined(CONFIG_XENO_ARM_SA1100)
    XENOMAI_SYSCALL5(__xn_sys_arch,
                     XENOMAI_SYSARCH_XCHG, ptr, x, size, &ret);
#else
    asm volatile("@ __xchg4\n"
"   swp     %0, %1, [%2]"
    : "=&r" (ret)
    : "r" (x), "r" (ptr)
    : "memory", "cc");
#endif
    return ret;
}

#define xnarch_atomic_xchg(ptr,x) \
    ({                                                                         \
    __typeof__(*(ptr)) _x_ = (x);                                          \
    (__typeof__(*(ptr))) __xchg((ptr), (unsigned long)_x_, sizeof(*(ptr)));\
    })

/*
 * Atomic operations lifted from linux/include/asm-arm/atomic.h 
 */
#if CONFIG_XENO_ARM_ARCH >= 6
static __inline__ void xnarch_atomic_set(xnarch_atomic_t *v, int i)
{
	unsigned long tmp;

	__asm__ __volatile__("@ xnarch_atomic_set\n"
"1:	ldrex	%0, [%1]\n"
"	strex	%0, %2, [%1]\n"
"	teq	%0, #0\n"
"	bne	1b"
	: "=&r" (tmp)
	: "r" (&v->counter), "r" (i)
	: "cc");
}

static __inline__ int
xnarch_atomic_cmpxchg(xnarch_atomic_t *ptr, int old, int newval)
{
	unsigned long oldval, res;

	do {
		__asm__ __volatile__("@ xnarch_atomic_cmpxchg\n"
		"ldrex	%1, [%2]\n"
		"mov	%0, #0\n"
		"teq	%1, %3\n"
		"strexeq %0, %4, [%2]\n"
		    : "=&r" (res), "=&r" (oldval)
		    : "r" (&ptr->counter), "Ir" (old), "r" (newval)
		    : "cc");
	} while (res);

	return oldval;
}

static __inline__ int xnarch_atomic_add_return(int i, xnarch_atomic_t *v)
{
    unsigned long tmp;
    int result;

    __asm__ __volatile__("@ xnarch_atomic_add_return\n"
"1: ldrex   %0, [%2]\n"
"   add     %0, %0, %3\n"
"   strex   %1, %0, [%2]\n"
"   teq     %1, #0\n"
"   bne     1b"
    : "=&r" (result), "=&r" (tmp)
    : "r" (&v->counter), "Ir" (i)
    : "cc");

    return result;
}

static __inline__ int xnarch_atomic_sub_return(int i, xnarch_atomic_t *v)
{
    unsigned long tmp;
    int result;

    __asm__ __volatile__("@ xnarch_atomic_sub_return\n"
"1: ldrex   %0, [%2]\n"
"   sub     %0, %0, %3\n"
"   strex   %1, %0, [%2]\n"
"   teq     %1, #0\n"
"   bne     1b"
    : "=&r" (result), "=&r" (tmp)
    : "r" (&v->counter), "Ir" (i)
    : "cc");

    return result;
}

static __inline__ void
xnarch_atomic_set_mask(unsigned long *addr, unsigned long mask)
{
    unsigned long tmp, tmp2;

    __asm__ __volatile__("@ xnarch_atomic_set_mask\n"
"1: ldrex   %0, [%2]\n"
"   orr     %0, %0, %3\n"
"   strex   %1, %0, [%2]\n"
"   teq     %1, #0\n"
"   bne     1b"
    : "=&r" (tmp), "=&r" (tmp2)
    : "r" (addr), "Ir" (mask)
    : "cc");
}

static __inline__ void
xnarch_atomic_clear_mask(unsigned long *addr, unsigned long mask)
{
    unsigned long tmp, tmp2;

    __asm__ __volatile__("@ xnarch_atomic_clear_mask\n"
"1: ldrex   %0, [%2]\n"
"   bic     %0, %0, %3\n"
"   strex   %1, %0, [%2]\n"
"   teq     %1, #0\n"
"   bne     1b"
    : "=&r" (tmp), "=&r" (tmp2)
    : "r" (addr), "Ir" (mask)
    : "cc");
}

#elif CONFIG_SMP
static __inline__ int xnarch_atomic_add_return(int i, xnarch_atomic_t *v)
{
    int ret;

    XENOMAI_SYSCALL4(__xn_sys_arch,
                     XENOMAI_SYSARCH_ATOMIC_ADD_RETURN, i, v, &ret);
    return ret;
}

static __inline__ int xnarch_atomic_sub_return(int i, xnarch_atomic_t *v)
{
    int ret;

    XENOMAI_SYSCALL4(__xn_sys_arch,
                     XENOMAI_SYSARCH_ATOMIC_ADD_RETURN, -i, v, &ret);
    return ret;
}

static inline void
xnarch_atomic_set_mask(unsigned long *addr, unsigned long mask)
{
    XENOMAI_SYSCALL3(__xn_sys_arch,
                     XENOMAI_SYSARCH_ATOMIC_SET_MASK, mask, addr);
}

static inline void
xnarch_atomic_clear_mask(unsigned long *addr, unsigned long mask)
{
    XENOMAI_SYSCALL3(__xn_sys_arch,
                     XENOMAI_SYSARCH_ATOMIC_CLEAR_MASK, mask, addr);
}
#else /* ARM_ARCH <= 5 && !CONFIG_SMP */

static __inline__ void xnarch_atomic_set(xnarch_atomic_t *ptr, int val)
{
	ptr->counter = val;
}

static __inline__ int
xnarch_atomic_cmpxchg(xnarch_atomic_t *ptr, int old, int newval)
{
        register int asm_old asm("r0") = old;
        register int asm_new asm("r1") = newval;
        register int *asm_ptr asm("r2") = (int *) &ptr->counter;
        register int asm_lr asm("lr");
	register int asm_tmp asm("r3");

	do {
		asm volatile ( \
			"mov %1, #0xffff0fff\n\t"	\
			"mov lr, pc\n\t"		 \
			"add pc, %1, #(0xffff0fc0 - 0xffff0fff)\n\t"	\
			: "+r"(asm_old), "=&r"(asm_tmp), "=r"(asm_lr)	\
			: "r"(asm_new), "r"(asm_ptr) \
			: "ip", "cc", "memory");
		if (likely(!asm_old))
			return old;
	} while ((asm_old = *asm_ptr) == old);
        return asm_old;
}

static __inline__ int xnarch_atomic_add_return(int i, xnarch_atomic_t *v)
{
	register int asm_old asm("r0");
	register int asm_new asm("r1");
	register int *asm_ptr asm("r2") = (int *) &v->counter;
        register int asm_lr asm("lr");
	register int asm_tmp asm("r3");

	asm volatile ( \
		"1: @ xnarch_atomic_add\n\t" \
		"ldr	%0, [%4]\n\t" \
		"mov	%1, #0xffff0fff\n\t" \
		"add	lr, pc, #4\n\t" \
		"add	%3, %0, %5\n\t"\
		"add	pc, %1, #(0xffff0fc0 - 0xffff0fff)\n\t" \
		"bcc	1b" \
		: "=&r" (asm_old), "=&r"(asm_tmp), "=r"(asm_lr), "=r"(asm_new) \
		: "r" (asm_ptr), "rIL"(i) \
		: "ip", "cc", "memory");
	return asm_new;
}

static __inline__ int xnarch_atomic_sub_return(int i, xnarch_atomic_t *v)
{
	register int asm_old asm("r0");
	register int asm_new asm("r1");
	register int *asm_ptr asm("r2") = (int *) &v->counter;
        register int asm_lr asm("lr");
	register int asm_tmp asm("r3");

	asm volatile ( \
		"1: @ xnarch_atomic_sub\n\t" \
		"ldr	%0, [%4]\n\t" \
		"mov	%1, #0xffff0fff\n\t" \
		"add	lr, pc, #4\n\t" \
		"sub	%3, %0, %5\n\t"\
		"add	pc, %1, #(0xffff0fc0 - 0xffff0fff)\n\t" \
		"bcc	1b" \
		: "=&r" (asm_old), "=&r"(asm_tmp), "=r"(asm_lr), "=r"(asm_new) \
		: "r" (asm_ptr), "rIL"(i) \
		: "ip", "cc", "memory");
	return asm_new;
}

static __inline__ void xnarch_atomic_set_mask(xnarch_atomic_t *v, long mask)
{
	register int asm_old asm("r0");
	register int asm_new asm("r1");
	register int *asm_ptr asm("r2") = (int *) &v->counter;
        register int asm_lr asm("lr");
	register int asm_tmp asm("r3");

	asm volatile ( \
		"1: @ xnarch_atomic_set_mask\n\t" \
		"ldr	%0, [%4]\n\t" \
		"mov	%1, #0xffff0fff\n\t" \
		"add	lr, pc, #4\n\t" \
		"orr	%3, %0, %5\n\t"\
		"add	pc, %1, #(0xffff0fc0 - 0xffff0fff)\n\t" \
		"bcc	1b" \
		: "=&r" (asm_old), "=&r"(asm_tmp), "=r"(asm_lr), "=r"(asm_new) \
		: "r" (asm_ptr), "rIL"(mask) \
		: "ip", "cc", "memory");
}

static __inline__ void xnarch_atomic_clear_mask(xnarch_atomic_t *v, long mask)
{
	register int asm_old asm("r0");
	register int asm_new asm("r1");
	register int *asm_ptr asm("r2") = (int *) &v->counter;
        register int asm_lr asm("lr");
	register int asm_tmp asm("r3");

	asm volatile ( \
		"1: @ xnarch_atomic_clear_mask\n\t" \
		"ldr	%0, [%4]\n\t" \
		"mov	%1, #0xffff0fff\n\t" \
		"add	lr, pc, #4\n\t" \
		"bic	%3, %0, %5\n\t" \
		"add	pc, %1, #(0xffff0fc0 - 0xffff0fff)\n\t" \
		"bcc	1b" \
		: "=&r" (asm_old), "=&r"(asm_tmp), "=r"(asm_lr), "=r"(asm_new) \
		: "r" (asm_ptr), "rIL"(mask) \
		: "ip", "cc", "memory");
}


#endif /* ARM_ARCH <= 5 && !CONFIG_SMP */

#if CONFIG_XENO_ARM_ARCH >= 7
#define xnarch_memory_barrier() \
	__asm__ __volatile__ ("dmb" : : : "memory")
#elif defined(CONFIG_XENO_CPU_XSC3) || CONFIG_XENO_ARM_ARCH == 6
#define xnarch_memory_barrier() \
	__asm__ __volatile__ ("mcr p15, 0, %0, c7, c10, 5" \
                              : : "r" (0) : "memory")
#else /* CONFIG_XENO_ARM_ARCH <= 5 */
#define xnarch_memory_barrier() \
	__asm__ __volatile__ ("": : :"memory")
#endif /* CONFIG_XENO_ARM_ARCH <= 5 */

#define xnarch_atomic_inc(pcounter)             (void) xnarch_atomic_add_return(1, pcounter)
#define xnarch_atomic_dec_and_test(pcounter)    (xnarch_atomic_sub_return(1, pcounter) == 0)

#define cpu_relax()                             xnarch_memory_barrier()
#define xnarch_read_memory_barrier()		xnarch_memory_barrier()
#define xnarch_write_memory_barrier()		xnarch_memory_barrier()

#endif /* __KERNEL__ */

typedef unsigned long atomic_flags_t;

#endif /* !_XENO_ASM_ARM_ATOMIC_H */

// vim: ts=4 et sw=4 sts=4

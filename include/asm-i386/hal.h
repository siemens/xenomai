/**
 *   @ingroup hal
 *   @file
 *
 *   Real-Time Hardware Abstraction Layer for x86.
 *
 *   Original RTAI/x86 HAL services from: \n
 *   Copyright &copy; 2000 Paolo Mantegazza, \n
 *   Copyright &copy; 2000 Steve Papacharalambous, \n
 *   Copyright &copy; 2000 Stuart Hughes, \n
 *   and others.
 *
 *   RTAI/x86 rewrite over Adeos: \n
 *   Copyright &copy; 2002,2003 Philippe Gerum.
 *   Major refactoring for Xenomai: \n
 *   Copyright &copy; 2004,2005 Philippe Gerum.
 *   Arithmetic/conversion routines: \n
 *   Copyright &copy; 2005 Gilles Chanteperdrix.
 *
 *   Xenomai is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License as
 *   published by the Free Software Foundation, Inc., 675 Mass Ave,
 *   Cambridge MA 02139, USA; either version 2 of the License, or (at
 *   your option) any later version.
 *
 *   Xenomai is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Xenomai; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *   02111-1307, USA.
 */

#ifndef _XENO_ASM_I386_HAL_H
#define _XENO_ASM_I386_HAL_H

#include <asm/xenomai/wrappers.h>

#define __rthal_u64tou32(ull, h, l) ({          \
    unsigned long long _ull = (ull);            \
    (l) = _ull & 0xffffffff;                    \
    (h) = _ull >> 32;                           \
})

#define __rthal_u64fromu32(h, l) ({             \
    unsigned long long _ull;                    \
    asm ( "": "=A"(_ull) : "d"(h), "a"(l));     \
    _ull;                                       \
})

/* const helper for rthal_uldivrem, so that the compiler will eliminate
   multiple calls with same arguments, at no additionnal cost. */
static inline __attribute_const__ unsigned long long
__rthal_uldivrem(const unsigned long long ull, const unsigned long d)
{
    unsigned long long ret;
    __asm__ ("divl %1" : "=A,A"(ret) : "r,?m"(d), "A,A"(ull));
    /* Exception if quotient does not fit on unsigned long. */
    return ret;
}

/* Fast long long division: when the quotient and remainder fit on 32 bits. */
static inline unsigned long __rthal_i386_uldivrem(unsigned long long ull,
                                                  const unsigned d,
                                                  unsigned long *const rp)
{
    unsigned long q, r;
    ull = __rthal_uldivrem(ull, d);
    __asm__ ( "": "=d"(r), "=a"(q) : "A"(ull));
    if(rp)
        *rp = r;
    return q;
}
#define rthal_uldivrem(ull, d, rp) __rthal_i386_uldivrem((ull),(d),(rp))

/* Division of an unsigned 96 bits ((h << 32) + l) by an unsigned 32 bits.
   Building block for ulldiv. */
static inline unsigned long long __rthal_div96by32 (const unsigned long long h,
                                                    const unsigned long l,
                                                    const unsigned long d,
                                                    unsigned long *const rp)
{
    u_long rh;
    const u_long qh = rthal_uldivrem(h, d, &rh);
    const unsigned long long t = __rthal_u64fromu32(rh, l);
    const u_long ql = rthal_uldivrem(t, d, rp);

    return __rthal_u64fromu32(qh, ql);
}

/* Slow long long division. Uses rthal_uldivrem, hence has the same property:
   the compiler removes redundant calls. */
static inline unsigned long long
__rthal_i386_ulldiv (const unsigned long long ull,
                     const unsigned d,
                     unsigned long *const rp)
{
    unsigned long h, l;
    __rthal_u64tou32(ull, h, l);
    return __rthal_div96by32(h, l, d, rp);
}
#define rthal_ulldiv(ull,d,rp) __rthal_i386_ulldiv((ull),(d),(rp))

#include <asm-generic/xenomai/hal.h>    /* Read the generic bits. */

#ifndef CONFIG_X86_WP_WORKS_OK
#error "Xenomai has to rely on the WP bit, CONFIG_M486 or better required"
#endif /* CONFIG_X86_WP_WORKS_OK */

typedef unsigned long long rthal_time_t;

static inline __attribute_const__ unsigned long ffnz (unsigned long ul)
{
    /* Derived from bitops.h's ffs() */
    __asm__("bsfl %1, %0"
            : "=r,r" (ul)
            : "r,?m"  (ul));
    return ul;
}

#ifndef __cplusplus
#include <asm/system.h>
#include <asm/io.h>
#include <asm/timex.h>
#include <asm/processor.h>
#include <io_ports.h>
#ifdef CONFIG_X86_LOCAL_APIC
#include <asm/fixmap.h>
#include <asm/apic.h>
#endif /* CONFIG_X86_LOCAL_APIC */
#include <asm/msr.h>
#include <asm/xenomai/atomic.h>
#include <asm/xenomai/smi.h>

#define RTHAL_8254_IRQ    0

#ifdef CONFIG_X86_LOCAL_APIC
#define RTHAL_APIC_TIMER_VECTOR    RTHAL_SERVICE_VECTOR3
#define RTHAL_APIC_TIMER_IPI       RTHAL_SERVICE_IPI3
#define RTHAL_APIC_ICOUNT          ((RTHAL_TIMER_FREQ + HZ/2)/HZ)
#define RTHAL_TIMER_IRQ 	   RTHAL_APIC_TIMER_IPI
#else  /* !CONFIG_X86_LOCAL_APIC */
#define RTHAL_TIMER_IRQ		   RTHAL_8254_IRQ
#endif /* CONFIG_X86_LOCAL_APIC */

#define RTHAL_NMICLK_FREQ	RTHAL_CPU_FREQ

static inline void rthal_grab_control(void)
{
    rthal_smi_init();
    rthal_smi_disable();
}

static inline void rthal_release_control(void)
{
    rthal_smi_restore();
}

#ifdef CONFIG_X86_TSC
static inline unsigned long long rthal_rdtsc (void)
{
    unsigned long long t;
    rthal_read_tsc(t);
    return t;
}
#else  /* !CONFIG_X86_TSC */
#define RTHAL_8254_COUNT2LATCH  0xfffe
void rthal_setup_8254_tsc(void);
rthal_time_t rthal_get_8254_tsc(void);
#define rthal_rdtsc() rthal_get_8254_tsc()
#endif /* CONFIG_X86_TSC */

static inline void rthal_timer_program_shot (unsigned long delay)
{
/* With head-optimization, callers are expected to have switched off
   hard-IRQs already -- no need for additional protection in this case. */
#ifndef CONFIG_XENO_OPT_PIPELINE_HEAD
    unsigned long flags;

    rthal_local_irq_save_hw(flags);
#endif /* CONFIG_XENO_OPT_PIPELINE_HEAD */
#ifdef CONFIG_X86_LOCAL_APIC
    if (!delay) {
        /* Kick the timer interrupt immediately. */
    	rthal_trigger_irq(RTHAL_APIC_TIMER_IPI);
    } else {
    /* Note: reading before writing just to work around the Pentium
       APIC double write bug. apic_read_around() expands to nil
       whenever CONFIG_X86_GOOD_APIC is set. --rpm */
    apic_read_around(APIC_LVTT);
    apic_write_around(APIC_LVTT,RTHAL_APIC_TIMER_VECTOR);
    apic_read_around(APIC_TMICT);
    apic_write_around(APIC_TMICT,delay);
    }
#else /* !CONFIG_X86_LOCAL_APIC */
    if (!delay)
	rthal_trigger_irq(RTHAL_8254_IRQ);
    else {
    	outb(delay & 0xff,0x40);
	outb(delay >> 8,0x40);
    }
#endif /* CONFIG_X86_LOCAL_APIC */
#ifndef CONFIG_XENO_OPT_PIPELINE_HEAD
    rthal_local_irq_restore_hw(flags);
#endif /* CONFIG_XENO_OPT_PIPELINE_HEAD */
}

static const char *const rthal_fault_labels[] = {
    [0] = "Divide error",
    [1] = "Debug",
    [2] = "",   /* NMI is not pipelined. */
    [3] = "Int3",
    [4] = "Overflow",
    [5] = "Bounds",
    [6] = "Invalid opcode",
    [7] = "FPU not available",
    [8] = "Double fault",
    [9] = "FPU segment overrun",
    [10] = "Invalid TSS",
    [11] = "Segment not present",
    [12] = "Stack segment",
    [13] = "General protection",
    [14] = "Page fault",
    [15] = "Spurious interrupt",
    [16] = "FPU error",
    [17] = "Alignment check",
    [18] = "Machine check",
    [19] = "SIMD error",
    [20] = NULL,
};

long rthal_strncpy_from_user(char *dst,
			     const char __user *src,
			     long count);
#endif /* !__cplusplus */

#endif /* !_XENO_ASM_I386_HAL_H */

/**
 *   @ingroup hal
 *   @file
 *
 *   Real-Time Hardware Abstraction Layer for the Blackfin.
 *
 *   Copyright &copy; 2005 Philippe Gerum.
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

#ifndef _XENO_ASM_BFINNOMMU_HAL_H
#define _XENO_ASM_BFINNOMMU_HAL_H

#include <asm-generic/xenomai/hal.h>	/* Read the generic bits. */
#include <asm/div64.h>

#define RTHAL_HOST_PERIOD      (1000000000UL/HZ)

typedef unsigned long long rthal_time_t;

#define __rthal_u64tou32(ull, h, l) ({                  \
        union { unsigned long long _ull;                \
            struct { u_long _l; u_long _h; } _s; } _u;  \
        _u._ull = (ull);                                \
        (h) = _u._s._h;                                 \
        (l) = _u._s._l;                                 \
        })

#define __rthal_u64fromu32(h, l) ({                     \
        union { unsigned long long _ull;                \
            struct { u_long _l; u_long _h; } _s; } _u;  \
        _u._s._h = (h);                                 \
        _u._s._l = (l);                                 \
        _u._ull;                                        \
        })

static inline unsigned long long rthal_ullmul(const unsigned long m0, 
					      const unsigned long m1)
{
    return (unsigned long long) m0 * m1;
}

static inline unsigned long long rthal_ulldiv (unsigned long long ull,
					       const unsigned long uld,
					       unsigned long *const rp)
{
    const unsigned long r = __div64_32(&ull, uld);
    if (rp) *rp = r;
    return ull;
}

#define rthal_uldivrem(ull,ul,rp) ((u_long) rthal_ulldiv((ull),(ul),(rp)))

static inline int rthal_imuldiv (int i, int mult, int div)
{
    /* Returns (int)i = (unsigned long long)i*(u_long)(mult)/(u_long)div. */
    const unsigned long long ull = rthal_ullmul(i, mult);
    return rthal_uldivrem(ull, div, NULL);
}

static inline __attribute_const__
unsigned long long __rthal_ullimd (const unsigned long long op,
                                   const unsigned long m,
                                   const unsigned long d)
{
    u_long oph, opl, tlh, tll, qh, rh, ql;
    unsigned long long th, tl;

    __rthal_u64tou32(op, oph, opl);
    tl = rthal_ullmul(opl, m);
    __rthal_u64tou32(tl, tlh, tll);
    th = rthal_ullmul(oph, m);
    th += tlh;

    qh = rthal_uldivrem(th, d, &rh);
    th = __rthal_u64fromu32(rh, tll);
    ql = rthal_uldivrem(th, d, NULL);
    return __rthal_u64fromu32(qh, ql);
}

static inline long long rthal_llimd (long long op,
                                     unsigned long m,
                                     unsigned long d)
{

    if(op < 0LL)
        return -__rthal_ullimd(-op, m, d);
    return __rthal_ullimd(op, m, d);
}

static inline __attribute_const__ unsigned long ffnz (unsigned long ul)
{
    return ffs(ul) - 1;
}

#ifndef __cplusplus
#include <asm/irqchip.h>
#include <asm/system.h>
#include <asm/mach/blackfin.h>
#include <asm/processor.h>
#include <asm/xenomai/atomic.h>

#define RTHAL_PERIODIC_TIMER_IRQ   IRQ_CORETMR
#define RTHAL_ONESHOT_TIMER_IRQ    IRQ_TMR0

#define rthal_irq_descp(irq)	(&irq_desc[(irq)])

#define rthal_grab_control()     do { } while(0)
#define rthal_release_control()  do { } while(0)

static inline unsigned long long rthal_rdtsc (void)
{
    unsigned long long t;
    rthal_read_tsc(t);
    return t;
}

static inline struct task_struct *rthal_root_host_task (int cpuid)
{
    return current;
}

static inline struct task_struct *rthal_current_host_task (int cpuid)
{
    return current;
}

static inline void rthal_timer_program_shot (unsigned long delay)
{
    if(!delay) delay = 10;

    if (*pTIMER_ENABLE & 1) {
    	/* The oneshot timer is enabled (and running);
	   force disable before reprogramming it. */
    	*pTIMER_DISABLE = 1;
    	*pTIMER_STATUS = 0x1000;
	__builtin_bfin_csync();
    }

    *pTIMER0_WIDTH = delay;
    *pTIMER_ENABLE = 1;	/* Enable TIMER0. */
    __builtin_bfin_csync();
}

    /* Private interface -- Internal use only */

unsigned long rthal_timer_host_freq(void);

void rthal_switch_context(unsigned long *out_kspp,
			  unsigned long *in_kspp);

static const char *const rthal_fault_labels[] = {
    [1] = "Single step",
    [4] = "TAS",
    [17] = "Performance Monitor Overflow",
    [33] = "Undefined instruction",
    [34] = "Illegal instruction",
    [36] = "Data access misaligned",
    [35] = "DCPLB fault",
    [37] = "Unrecoverable event",
    [38] = "DCPLB fault",
    [39] = "DCPLB fault",
    [40] = "Watchpoint",
    [42] = "Instruction fetch misaligned",
    [41] = "Undef",
    [43] = "ICPLB fault",
    [44] = "ICPLB fault",
    [45] = "ICPLB fault",
    [46] = "Illegal resource",
    [47] = NULL
};

#endif /* !__cplusplus */

#endif /* !_XENO_ASM_BFINNOMMU_HAL_H */

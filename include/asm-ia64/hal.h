/**
 *   @ingroup hal
 *   @file
 *
 *   Real-Time Hardware Abstraction Layer for the ia64 architecture.
 *
 *   Copyright &copy; 2002-2004 Philippe Gerum
 *   Copyright &copy; 2004 The HYADES project <http://www.hyades-itea.org>
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

#ifndef _XENO_ASM_IA64_HAL_H
#define _XENO_ASM_IA64_HAL_H

#include <asm-generic/xenomai/hal.h>	/* Read the generic bits. */

typedef unsigned long long rthal_time_t;

#define __rthal_u64tou32(ull, h, l) ({          \
    unsigned long long _ull = (ull);            \
    (l) = _ull & 0xffffffff;                    \
    (h) = _ull >> 32;                           \
})

#define __rthal_u64fromu32(h, l) ({             \
    (((unsigned long long) (h)) << 32) + (l);   \
})

static inline unsigned long long rthal_ullmul(const unsigned m0, 
                                              const unsigned m1)
{
    return (unsigned long long) m0 * m1;
}

static inline unsigned long long rthal_ulldiv (unsigned long long ull,
                                               const unsigned uld,
                                               unsigned long *const rp)
{
    const unsigned long long result = ull / uld;

    if (rp)
        *rp = ull % uld;

    return result;
}

#define rthal_uldivrem(ull,ul,rp) ((u_long) rthal_ulldiv((ull),(ul),(rp)))

static inline __attribute_const__ int rthal_imuldiv (const int i,
                                                     const int mult,
                                                     const int div) {

    /* Returns (int)i = (unsigned long long)i*(u_long)(mult)/(u_long)div. */
    const unsigned long long ull = rthal_ullmul(i, mult);
    return rthal_uldivrem(ull, div, NULL);
}

static inline __attribute_const__
unsigned long long __rthal_ullimd (const unsigned long long op,
                                   const unsigned m,
                                   const unsigned d)
{
    unsigned oph, opl, tlh, tll, qh, ql;
    unsigned long long th, tl;
    unsigned long rh;

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
                                     unsigned m,
                                     unsigned d)
{

    if(op < 0LL)
        return -__rthal_ullimd(-op, m, d);
    return __rthal_ullimd(op, m, d);
}

static inline __attribute_const__ unsigned long ffnz (unsigned long ul)
{
    unsigned long r;
    asm ("popcnt %0=%1" : "=r" (r) : "r" ((ul-1) & ~ul));
    return r;
}

#ifndef __cplusplus
#include <asm/system.h>
#include <asm/xenomai/atomic.h>
#include <asm/processor.h>
#include <asm/delay.h>          /* For ia64_get_itc / ia64_set_itm */

#define RTHAL_TIMER_VECTOR      IPIPE_SERVICE_VECTOR3
#define RTHAL_TIMER_IRQ         IPIPE_SERVICE_IPI3
#define RTHAL_HOST_TIMER_VECTOR IA64_TIMER_VECTOR
#define RTHAL_HOST_TIMER_IRQ    __ia64_local_vector_to_irq(IA64_TIMER_VECTOR)

#define rthal_irq_descp(irq)  irq_descp(irq)
#define rthal_itm_next        __ipipe_itm_next
#define rthal_tick_irq        __ipipe_tick_irq

#define rthal_grab_control()     do { } while(0)
#define rthal_release_control()  do { } while(0)

static inline unsigned long long rthal_rdtsc (void)
{
    unsigned long long t;
    rthal_read_tsc(t);
    return t;
}

static inline void rthal_timer_program_shot (unsigned long delay)
{
    unsigned long flags;
    if (!delay) { delay = 10; }
    rthal_local_irq_save_hw(flags);
    ia64_set_itm(ia64_get_itc() + delay);
    rthal_local_irq_restore_hw(flags);
}

    /* Private interface -- Internal use only */

void rthal_switch_context(void *out_tcb,
			  void *in_tcb);

void rthal_prepare_stack(unsigned long stackbase);

static const char *const rthal_fault_labels[] = {
    [0] = "General exception",
    [1] = "FPU disabled",
    [2] = "NaT consumption",
    [3] = "Unsupported data reference",
    [4] = "Debug",
    [5] = "FPU fault",
    [6] = "Unimplemented instruction address",
    [7] = "ia32 exception",
    [8] = "Generic fault",
    [9] = "Page fault",
    [10] = NULL
};

#endif /* !__cplusplus */

#endif /* !_XENO_ASM_IA64_HAL_H */

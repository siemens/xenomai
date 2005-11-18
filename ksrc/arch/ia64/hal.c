/**
 *   @ingroup hal
 *   @file
 *
 *   Adeos-based Real-Time Abstraction Layer for ia64.
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
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *   02111-1307, USA.
 */

/**
 * @addtogroup hal
 *
 * ia64-specific HAL services.
 *
 *@{*/

#include <linux/version.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/console.h>
#include <linux/interrupt.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <asm/xenomai/hal.h>
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif /* CONFIG_PROC_FS */
#include <stdarg.h>

static void rthal_adjust_before_relay (unsigned irq, void *cookie)
{
    rthal_itm_next[rthal_processor_id()] = ia64_get_itc();
    rthal_propagate_irq(irq);
}

static void rthal_set_itv(void)
{
    rthal_itm_next[rthal_processor_id()] = ia64_get_itc();
    ia64_set_itv(irq_to_vector(rthal_tick_irq));
}

static void rthal_timer_set_irq (unsigned tick_irq)
{
    unsigned long flags;

    flags = rthal_critical_enter(&rthal_set_itv);
    rthal_tick_irq = tick_irq;
    rthal_set_itv();
    rthal_critical_exit(flags);
}

int rthal_timer_request (void (*handler)(void),
			 unsigned long nstick)
{
    unsigned long flags;

    flags = rthal_critical_enter(NULL);

    rthal_irq_release(RTHAL_TIMER_IRQ);
    
    rthal_set_timer(nstick);

    if (rthal_irq_request(RTHAL_TIMER_IRQ,
                          (rthal_irq_handler_t) handler,
			  NULL,
                          NULL) < 0)
        {
        rthal_critical_exit(flags);
        return -EINVAL;
        }

    if (rthal_irq_request(RTHAL_HOST_TIMER_IRQ,
                          &rthal_adjust_before_relay,
			  NULL,
                          NULL) < 0)
        {
        rthal_critical_exit(flags);
        return -EINVAL;
        }

    rthal_critical_exit(flags);

    rthal_timer_set_irq(RTHAL_TIMER_IRQ);

    return 0;
}

void rthal_timer_release (void)

{
    unsigned long flags;

    rthal_timer_set_irq(RTHAL_HOST_TIMER_IRQ);
    rthal_reset_timer();
    flags = rthal_critical_enter(NULL);        
    rthal_irq_release(RTHAL_TIMER_IRQ);
    rthal_irq_release(RTHAL_HOST_TIMER_IRQ);
    rthal_critical_exit(flags);
}

unsigned long rthal_timer_calibrate (void)

{
    unsigned long flags, delay;
    rthal_time_t t, dt;
    int i;

    delay = RTHAL_CPU_FREQ; /* 1s */
    
    flags = rthal_critical_enter(NULL);

    t = rthal_rdtsc();

    for (i = 0; i < 10000; i++)
        rthal_timer_program_shot(delay);

    dt = rthal_rdtsc() - t;

    rthal_critical_exit(flags);

    return rthal_imuldiv(dt,100000,RTHAL_CPU_FREQ);
}

static inline int do_exception_event (unsigned event, unsigned domid, void *data)

{
    rthal_declare_cpuid;

    rthal_load_cpuid();

    if (domid == RTHAL_DOMAIN_ID)
	{
	rthal_realtime_faults[cpuid][event]++;

	if (rthal_trap_handler != NULL &&
	    test_bit(cpuid,&rthal_cpu_realtime) &&
	    rthal_trap_handler(event,domid,data) != 0)
	    return RTHAL_EVENT_STOP;
	}

    return RTHAL_EVENT_PROPAGATE;
}

RTHAL_DECLARE_EVENT(exception_event);

static inline void do_rthal_domain_entry (void)

{
    unsigned trapnr;

    /* Trap all faults. */
    for (trapnr = 0; trapnr < RTHAL_NR_FAULTS; trapnr++)
	rthal_catch_exception(trapnr,&exception_event);

    printk(KERN_INFO "Xenomai: hal/ia64 started.\n");
}

RTHAL_DECLARE_DOMAIN(rthal_domain_entry);

int rthal_arch_init (void)

{
    if (rthal_cpufreq_arg == 0)
	{
	adsysinfo_t sysinfo;
	rthal_get_sysinfo(&sysinfo);
	rthal_cpufreq_arg = (unsigned long)sysinfo.cpufreq;
	}

    if (rthal_timerfreq_arg == 0)
	rthal_timerfreq_arg = rthal_cpufreq_arg;

    return 0;
}

void rthal_arch_cleanup (void)

{
    /* Nothing to cleanup so far. */
    printk(KERN_INFO "Xenomai: hal/ia64 stopped.\n");
}

/*@}*/

EXPORT_SYMBOL(rthal_arch_init);
EXPORT_SYMBOL(rthal_arch_cleanup);
EXPORT_SYMBOL(rthal_switch_context);
EXPORT_SYMBOL(rthal_prepare_stack);

/**
 *   @ingroup hal
 *   @file
 *
 *   Adeos-based Real-Time Abstraction Layer for the Blackfin
 *   architecture.
 *
 *   Copyright (C) 2005 Philippe Gerum.
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
 * Blackfin-specific HAL services.
 *
 *@{*/

#include <linux/config.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <asm/system.h>
#include <asm/atomic.h>
#include <asm/irqchip.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <asm/xenomai/hal.h>

#ifdef CONFIG_XENO_OPT_TIMING_PERIODIC

#define PERIODIC_TIME_SCALE 1
#define CLOCKS_PER_TICK(hz) (get_cclk() / (hz) / PERIODIC_TIME_SCALE)

int rthal_periodic_p;

static inline void rthal_set_aperiodic(void)
{
    rthal_periodic_p = 0;
}
#else /* !CONFIG_XENO_OPT_TIMING_PERIODIC */
#define rthal_set_aperiodic() do { } while(0)
#endif /* CONFIG_XENO_OPT_TIMING_PERIODIC */

static struct {

    unsigned long flags;
    int count;

} rthal_linux_irq[IPIPE_NR_XIRQS];

/* Acknowledge the core timer IRQ. This routine does nothing, except
   preventing Linux to mask the IRQ. */

static int rthal_timer_ack (unsigned irq)
{
    return 1;
}

#ifdef CONFIG_XENO_HW_NMI_DEBUG_LATENCY

asmlinkage void irq_panic(int reason, struct pt_regs *regs);

static void rthal_latency_above_max(struct pt_regs *regs)
{
    rthal_emergency_console();
    printk("NMI watchdog detected timer latency above %u us\n",
	   rthal_maxlat_us);
    dump_stack();
    irq_panic(IRQ_NMI,regs);
}

#endif /* CONFIG_XENO_HW_NMI_DEBUG_LATENCY_MAX */

int rthal_timer_request (void (*handler)(void),
			 unsigned long nstick)
{
    unsigned long flags;
    int err;

    flags = rthal_critical_enter(NULL);

    if (nstick > 0) {
#ifdef CONFIG_XENO_OPT_TIMING_PERIODIC
	/* Periodic setup. */
	*pTCNTL = 1;
	__builtin_bfin_csync();
	*pTSCALE = (PERIODIC_TIME_SCALE - 1);
	*pTCOUNT = *pTPERIOD = CLOCKS_PER_TICK(1000000000L / nstick) - 1;
	__builtin_bfin_csync();
	*pTCNTL = 7;		/* Auto-reload on. */
	rthal_periodic_p = 1;
#else /* !CONFIG_XENO_OPT_TIMING_PERIODIC */
        return -ENOSYS;
#endif /* CONFIG_XENO_OPT_TIMING_PERIODIC */
    }
    else {
	/* Oneshot setup. We still use the core timer, but without
	   auto-reload. */
	*pTCNTL = 1;	/* Power up the timer, but leave it disabled. */
	__builtin_bfin_csync();
	*pTSCALE = 0;
	__builtin_bfin_csync();
	rthal_set_aperiodic();
    }

    rthal_irq_release(RTHAL_TIMER_IRQ);

    err = rthal_irq_request(RTHAL_TIMER_IRQ,
			    (rthal_irq_handler_t)handler,
			    &rthal_timer_ack,
			    NULL);

    rthal_critical_exit(flags);

    if (err)
	return err;

    rthal_irq_enable(RTHAL_TIMER_IRQ);

#ifdef CONFIG_XENO_HW_NMI_DEBUG_LATENCY
    if (nstick == 0)	/* This only works in aperiodic mode. */
	rthal_nmi_init(&rthal_latency_above_max);
#endif /* CONFIG_XENO_HW_NMI_DEBUG_LATENCY */

    return 0;
}

void rthal_timer_release (void)

{
#ifdef CONFIG_XENO_HW_NMI_DEBUG_LATENCY
    rthal_nmi_release();
#endif /* CONFIG_XENO_HW_NMI_DEBUG_LATENCY */
    *pTCNTL = 0;	/* Power down the core timer. */
    rthal_irq_disable(RTHAL_TIMER_IRQ);
    rthal_irq_release(RTHAL_TIMER_IRQ);
}

unsigned long rthal_timer_calibrate (void)

{
    return (1000000000 / RTHAL_CPU_FREQ) * 100;	/* 100 CPU cycles -- FIXME */
}

int rthal_irq_enable (unsigned irq)

{
    if (irq >= IPIPE_NR_XIRQS)
	return -EINVAL;

    if (rthal_irq_descp(irq)->chip->unmask == NULL)
	return -ENODEV;

    rthal_irq_descp(irq)->chip->unmask(irq);

    return 0;
}

int rthal_irq_disable (unsigned irq)
{

    if (irq >= IPIPE_NR_XIRQS)
	return -EINVAL;

    if (rthal_irq_descp(irq)->chip->mask == NULL)
	return -ENODEV;

    rthal_irq_descp(irq)->chip->mask(irq);

    return 0;
}

int rthal_irq_end (unsigned irq)

{
    if (irq >= IPIPE_NR_XIRQS)
	return -EINVAL;

    if (rthal_irq_descp(irq)->chip->unmask == NULL)
	return -ENODEV;

    rthal_irq_descp(irq)->chip->unmask(irq);

    return 0;
}

int rthal_irq_host_request (unsigned irq,
			    irqreturn_t (*handler)(int irq,
						   void *dev_id,
						   struct pt_regs *regs), 
			    char *name,
			    void *dev_id)
{
    if (irq >= IPIPE_NR_XIRQS || !handler)
	return -EINVAL;

    if (rthal_linux_irq[irq].count++ == 0 && rthal_irq_descp(irq)->action)
	{
	rthal_linux_irq[irq].flags = rthal_irq_descp(irq)->action->flags;
	rthal_irq_descp(irq)->action->flags |= SA_SHIRQ;
	}

    return request_irq(irq,handler,SA_SHIRQ,name,dev_id);
}

int rthal_irq_host_release (unsigned irq, void *dev_id)

{
    if (irq >= IPIPE_NR_XIRQS || rthal_linux_irq[irq].count == 0)
	return -EINVAL;

    free_irq(irq,dev_id);

    if (--rthal_linux_irq[irq].count == 0 && rthal_irq_descp(irq)->action)
	rthal_irq_descp(irq)->action->flags = rthal_linux_irq[irq].flags;

    return 0;
}

static inline int do_exception_event (unsigned event, unsigned domid, void *data)

{
    rthal_declare_cpuid;

    rthal_load_cpuid();

    if (domid == RTHAL_DOMAIN_ID)
	{
	rthal_realtime_faults[cpuid][event]++;

	if (rthal_trap_handler != NULL &&
	    test_bit(cpuid,(void *)&rthal_cpu_realtime) &&
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

    printk(KERN_INFO "Xenomai: hal/blackfin started.\n");
}

RTHAL_DECLARE_DOMAIN(rthal_domain_entry);

int rthal_arch_init (void)

{
    if (rthal_cpufreq_arg == 0)
	rthal_cpufreq_arg = (unsigned long)rthal_get_cpufreq();

    if (rthal_timerfreq_arg == 0)
	/* Define the global timer frequency as being the one of the
	   core timer, which is running at the core clock (CCLK)
	   rate. */
	rthal_timerfreq_arg = get_cclk();

    return 0;
}

void rthal_arch_cleanup (void)

{
    printk(KERN_INFO "Xenomai: hal/blackfin stopped.\n");
}

/*@}*/

EXPORT_SYMBOL(rthal_arch_init);
EXPORT_SYMBOL(rthal_arch_cleanup);
EXPORT_SYMBOL(rthal_thread_switch);
EXPORT_SYMBOL(rthal_thread_trampoline);
EXPORT_SYMBOL(rthal_defer_switch_p);

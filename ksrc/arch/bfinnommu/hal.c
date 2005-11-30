/**
 *   @ingroup hal
 *   @file
 *
 *   Adeos-based Real-Time Abstraction Layer for the Blackfin
 *   architecture.
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
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif /* CONFIG_PROC_FS */
#include <stdarg.h>

static struct {

    unsigned long flags;
    int count;

} rthal_linux_irq[IPIPE_NR_XIRQS];

static int rthal_periodic_p;

int rthal_timer_request (void (*handler)(void),
			 unsigned long nstick)
{
    unsigned long flags;
    int err;

    flags = rthal_critical_enter(NULL);

    if (nstick > 0)
	{
	/* Periodic setup --
	   Use the built-in Adeos service directly. */
	err = rthal_set_timer(nstick);
	rthal_periodic_p = 1;
	}
    else
	{
	/* Oneshot setup. */
	rthal_periodic_p = 0;
	/* FIXME */
	}

    rthal_irq_release(RTHAL_TIMER_IRQ);

    err = rthal_irq_request(RTHAL_TIMER_IRQ,
			    (rthal_irq_handler_t)handler,
			    NULL,
			    NULL);

    rthal_critical_exit(flags);

    return err;
}

void rthal_timer_release (void)

{
    unsigned long flags;

    flags = rthal_critical_enter(NULL);

    if (rthal_periodic_p)
	rthal_reset_timer();
    else
	{
	/* FIXME */
	}

    rthal_irq_release(RTHAL_TIMER_IRQ);

    rthal_critical_exit(flags);
}

unsigned long rthal_timer_calibrate (void)

{
    return 1000000000 / RTHAL_CPU_FREQ;
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
	rthal_timerfreq_arg = rthal_cpufreq_arg;

    return 0;
}

void rthal_arch_cleanup (void)

{
    /* Nothing to cleanup so far. */
    printk(KERN_INFO "Xenomai: hal/blackfin stopped.\n");
}

/*@}*/

EXPORT_SYMBOL(rthal_arch_init);
EXPORT_SYMBOL(rthal_arch_cleanup);
EXPORT_SYMBOL(rthal_switch_context);

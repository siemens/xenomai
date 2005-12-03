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

#ifdef CONFIG_XENO_HW_PERIODIC_TIMER
int rthal_periodic_p;

static inline void rthal_set_aperiodic(void)
{
    rthal_periodic_p = 0;
}
#else /* !CONFIG_XENO_HW_PERIODIC_TIMER */
#define rthal_set_aperiodic() do { } while(0)
#endif /* CONFIG_XENO_HW_PERIODIC_TIMER */

static struct {

    unsigned long flags;
    int count;

} rthal_linux_irq[IPIPE_NR_XIRQS];

/* Acknowledge the timer IRQ. In periodic mode, this routine does
   nothing, except preventing Linux to mask the core timer IRQ; in
   aperiodic mode, we additionally make sure to deassert the interrupt
   bit for TIMER0. In either case, the interrupt channel is always
   kept unmasked. */

static int rthal_timer_ack (unsigned irq)
{
    if (!rthal_periodic_p) {
    	/* Clear TIMER0 interrupt and re-enable IRQ channel. */
    	*pTIMER_STATUS = 1;
	__builtin_bfin_csync();
    }
    return 1;
}

int rthal_timer_request (void (*handler)(void),
			 unsigned long nstick)
{
    unsigned long flags;
    unsigned irq;
    int err;

    flags = rthal_critical_enter(NULL);

    if (nstick > 0) {
#ifdef CONFIG_XENO_HW_PERIODIC_TIMER
	/* Periodic setup -- Use the built-in Adeos service directly
	   which relies on the core timer. */
	err = rthal_set_timer(nstick);
	irq = RTHAL_PERIODIC_TIMER_IRQ;
	rthal_periodic_p = 1;
#else /* !CONFIG_XENO_HW_PERIODIC_TIMER */
        return -ENOSYS;
#endif /* CONFIG_XENO_HW_PERIODIC_TIMER */
	}
    else
	{
	/* Oneshot setup. We use TIMER0 in PWM_OUT, single pulse mode. */
	*pTIMER_DISABLE = 1;	/* Disable TIMER0 for now. */
	__builtin_bfin_csync();
	*pTIMER0_CONFIG = 0x11;	/* IRQ enable, single pulse, PWM_OUT, SCLKed */
	__builtin_bfin_csync();
	irq = RTHAL_APERIODIC_TIMER_IRQ;
	rthal_irq_enable(irq);
	rthal_set_aperiodic();
	}

    rthal_irq_release(irq);

    err = rthal_irq_request(irq,
			    (rthal_irq_handler_t)handler,
			    &rthal_timer_ack,
			    NULL);

    rthal_critical_exit(flags);

    return err;
}

void rthal_timer_release (void)

{
    unsigned long flags;
    unsigned irq;

    flags = rthal_critical_enter(NULL);

    if (rthal_periodic_p)
	{
	rthal_reset_timer();
	irq = RTHAL_PERIODIC_TIMER_IRQ;
	}
    else
	{
	*pTIMER_DISABLE = 1;	/* Disable TIMER0. */
	__builtin_bfin_csync();
	irq = RTHAL_APERIODIC_TIMER_IRQ;
	rthal_irq_disable(irq);
	}

    rthal_irq_release(irq);

    rthal_critical_exit(flags);
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

unsigned long rthal_timer_host_freq (void)
{
    /* In periodic timing, we divert the core timer for our own
       ticking, so we need to relay a Linux timer tick according to
       the HZ frequency. In aperiodic timing, we use TIMER0, leaving
       the core timer untouched, so we don't need to relay any host
       tick since we don't divert it in the first place. */
    return rthal_periodic_p ? RTHAL_HOST_PERIOD : 0;
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
    unsigned long get_sclk(void);	/* System clock freq (HZ) */

    if (rthal_cpufreq_arg == 0)
	rthal_cpufreq_arg = (unsigned long)rthal_get_cpufreq();

    if (rthal_timerfreq_arg == 0)
	/* Define the global timer frequency as being the one of the
	   aperiodic timer (TIMER0), which is running at the system
	   clock (SCLK) rate. */
	rthal_timerfreq_arg = get_sclk();

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

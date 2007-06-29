/**
 *   @ingroup hal
 *   @file
 *
 *   Adeos-based Real-Time Abstraction Layer for x86_64.
 *   Derived from the Xenomai/i386 HAL.
 *
 *   Copyright (C) 2007 Philippe Gerum.
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
 * x86_64-specific HAL services.
 *
 *@{*/

#include <linux/version.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/console.h>
#include <asm/system.h>
#include <asm/hardirq.h>
#include <asm/desc.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <asm/fixmap.h>
#include <asm/bitops.h>
#include <asm/mpspec.h>
#include <asm/io_apic.h>
#include <asm/apic.h>
#include <asm/xenomai/hal.h>
#include <asm/mach_apic.h>
#include <stdarg.h>

static struct {

	unsigned long flags;
	int count;

} rthal_linux_irq[IPIPE_NR_XIRQS];

static long long rthal_timers_sync_time;

static inline void rthal_setup_periodic_apic(int count, int vector)
{
	apic_write(APIC_LVTT, APIC_LVT_TIMER_PERIODIC | vector);
	apic_write(APIC_TMICT, count);
}

static inline void rthal_setup_oneshot_apic(int vector)
{
	apic_write(APIC_LVTT, vector);
}

static void rthal_critical_sync(void)
{
	long long sync_time;

	switch (rthal_sync_op) {
	case 1:
		sync_time = rthal_timers_sync_time;

		while (rthal_rdtsc() < sync_time) ;

		rthal_setup_oneshot_apic(RTHAL_APIC_TIMER_VECTOR);
		break;

	case 2:

		rthal_setup_periodic_apic(RTHAL_APIC_ICOUNT,
					  LOCAL_TIMER_VECTOR);
		break;
	}
}

irqreturn_t rthal_broadcast_to_local_timers(int irq, void *dev_id)
{
#ifdef CONFIG_SMP
	send_IPI_all(LOCAL_TIMER_VECTOR);
#else
	rthal_trigger_irq(ipipe_apic_vector_irq(LOCAL_TIMER_VECTOR));
#endif
	return IRQ_HANDLED;
}

unsigned long rthal_timer_calibrate(void)
{
	unsigned long flags;
	rthal_time_t t, dt;
	int i;

	flags = rthal_critical_enter(NULL);

	t = rthal_rdtsc();

	for (i = 0; i < 10000; i++) {
		apic_write(APIC_LVTT,
			   APIC_LVT_TIMER_PERIODIC | LOCAL_TIMER_VECTOR);
		apic_write(APIC_TMICT, RTHAL_APIC_ICOUNT);
	}

	dt = (rthal_rdtsc() - t) / 2;

	rthal_critical_exit(flags);

#ifdef CONFIG_IPIPE_TRACE_IRQSOFF
	/* reset the max trace, it contains the excessive calibration now */
	rthal_trace_max_reset();
#endif /* CONFIG_IPIPE_TRACE_IRQSOFF */

	return rthal_imuldiv(dt, 100000, RTHAL_CPU_FREQ);
}

int rthal_timer_request(void (*handler)(void), int cpu)
{
	long long sync_time;
	unsigned long flags;

	if (cpu > 0)
		goto out;

	flags = rthal_critical_enter(rthal_critical_sync);

	rthal_sync_op = 1;

	rthal_timers_sync_time = rthal_rdtsc() +
	    rthal_imuldiv(LATCH, RTHAL_CPU_FREQ, CLOCK_TICK_RATE);

	sync_time = rthal_timers_sync_time;

	while (rthal_rdtsc() < sync_time) ;

	rthal_setup_oneshot_apic(RTHAL_APIC_TIMER_VECTOR);

	rthal_irq_request(RTHAL_APIC_TIMER_IPI,
			  (rthal_irq_handler_t) handler, NULL, NULL);

	rthal_critical_exit(flags);

	rthal_irq_host_request(RTHAL_8254_IRQ,
			       &rthal_broadcast_to_local_timers,
			       "rthal_broadcast_timer",
			       &rthal_broadcast_to_local_timers);
out:
	return 0;
}

void rthal_timer_release(int cpu)
{
	unsigned long flags;

	if (cpu > 0)
		return;

	rthal_irq_host_release(RTHAL_8254_IRQ,
			       &rthal_broadcast_to_local_timers);

	flags = rthal_critical_enter(&rthal_critical_sync);

	rthal_sync_op = 2;
	rthal_setup_periodic_apic(RTHAL_APIC_ICOUNT, LOCAL_TIMER_VECTOR);
	rthal_irq_release(RTHAL_APIC_TIMER_IPI);

	rthal_critical_exit(flags);
}

int rthal_irq_host_request(unsigned irq,
			   rthal_irq_host_handler_t handler,
			   char *name, void *dev_id)
{
	unsigned long flags;

	if (irq >= IPIPE_NR_XIRQS || !handler)
		return -EINVAL;

	spin_lock_irqsave(&rthal_irq_descp(irq)->lock, flags);

	if (rthal_linux_irq[irq].count++ == 0 && rthal_irq_descp(irq)->action) {
		rthal_linux_irq[irq].flags =
		    rthal_irq_descp(irq)->action->flags;
		rthal_irq_descp(irq)->action->flags |= IRQF_SHARED;
	}

	spin_unlock_irqrestore(&rthal_irq_descp(irq)->lock, flags);

	return request_irq(irq, handler, IRQF_SHARED, name, dev_id);
}

int rthal_irq_host_release(unsigned irq, void *dev_id)
{
	unsigned long flags;

	if (irq >= NR_IRQS || rthal_linux_irq[irq].count == 0)
		return -EINVAL;

	free_irq(irq, dev_id);

	spin_lock_irqsave(&rthal_irq_descp(irq)->lock, flags);

	if (--rthal_linux_irq[irq].count == 0 && rthal_irq_descp(irq)->action)
		rthal_irq_descp(irq)->action->flags =
		    rthal_linux_irq[irq].flags;

	spin_unlock_irqrestore(&rthal_irq_descp(irq)->lock, flags);

	return 0;
}

int rthal_irq_enable(unsigned irq)
{
	if (irq >= NR_IRQS)
		return -EINVAL;

	rthal_irq_desc_status(irq) &= ~IRQ_DISABLED;

	return rthal_irq_chip_enable(irq);
}

int rthal_irq_disable(unsigned irq)
{

	if (irq >= NR_IRQS)
		return -EINVAL;

	rthal_irq_desc_status(irq) |= IRQ_DISABLED;

	return rthal_irq_chip_disable(irq);
}

int rthal_irq_end(unsigned irq)
{
	if (irq >= NR_IRQS)
		return -EINVAL;

	return rthal_irq_chip_end(irq);
}

static inline int do_exception_event(unsigned event, unsigned domid, void *data)
{
	rthal_declare_cpuid;

	rthal_load_cpuid();

	if (domid == RTHAL_DOMAIN_ID) {
		rthal_realtime_faults[cpuid][event]++;

		if (rthal_trap_handler != NULL &&
		    rthal_trap_handler(event, domid, data) != 0)
			return RTHAL_EVENT_STOP;
	}

	return RTHAL_EVENT_PROPAGATE;
}

RTHAL_DECLARE_EVENT(exception_event);

static inline void do_rthal_domain_entry(void)
{
	unsigned trapnr;

	/* Trap all faults. */
	for (trapnr = 0; trapnr < RTHAL_NR_FAULTS; trapnr++)
		rthal_catch_exception(trapnr, &exception_event);

	printk(KERN_INFO "Xenomai: hal/x86_64 started.\n");
}

RTHAL_DECLARE_DOMAIN(rthal_domain_entry);

int rthal_arch_init(void)
{
	if (rthal_cpufreq_arg == 0)
		/* FIXME: 4Ghz barrier is close... */
		rthal_cpufreq_arg = rthal_get_cpufreq();

	if (rthal_timerfreq_arg == 0)
		rthal_timerfreq_arg = apic_read(APIC_TMICT) * HZ;

	return 0;
}

void rthal_arch_cleanup(void)
{
	printk(KERN_INFO "Xenomai: hal/x86_64 stopped.\n");
}

/*@}*/

EXPORT_SYMBOL(rthal_arch_init);
EXPORT_SYMBOL(rthal_arch_cleanup);

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
#include <linux/bitops.h>
#include <asm/system.h>
#include <asm/hardirq.h>
#include <asm/desc.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <asm/fixmap.h>
#include <asm/mpspec.h>
#include <asm/io_apic.h>
#include <asm/apic.h>
#include <asm/xenomai/hal.h>
#include <asm/mach_apic.h>
#include <stdarg.h>

#define RTHAL_SET_ONESHOT_XENOMAI	1
#define RTHAL_SET_ONESHOT_LINUX		2
#define RTHAL_SET_PERIODIC		3

static enum { /* <!> Must follow enum clock_event_mode */
	KTIMER_MODE_UNUSED = 0,
	KTIMER_MODE_SHUTDOWN,
	KTIMER_MODE_PERIODIC,
	KTIMER_MODE_ONESHOT,
} rthal_ktimer_saved_mode;

static struct {
	unsigned long flags;
	int count;
} rthal_linux_irq[IPIPE_NR_XIRQS];

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
	switch (rthal_sync_op) {
	case RTHAL_SET_ONESHOT_XENOMAI:
		rthal_setup_oneshot_apic(RTHAL_APIC_TIMER_VECTOR);
		break;

	case RTHAL_SET_ONESHOT_LINUX:
		rthal_setup_oneshot_apic(LOCAL_TIMER_VECTOR);
		/* We need to keep the timing cycle alive for the kernel. */
		rthal_trigger_irq(ipipe_apic_vector_irq(LOCAL_TIMER_VECTOR));
		break;

	case RTHAL_SET_PERIODIC:
		rthal_setup_periodic_apic(RTHAL_APIC_ICOUNT, LOCAL_TIMER_VECTOR);
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
	unsigned long v, flags;
	rthal_time_t t, dt;
	int i;

	flags = rthal_critical_enter(NULL);

	t = rthal_rdtsc();

	for (i = 0; i < 20; i++) {
		v = apic_read(APIC_TMICT);
		apic_write(APIC_TMICT, v);
	}

	dt = (rthal_rdtsc() - t) / 2;

	rthal_critical_exit(flags);

#ifdef CONFIG_IPIPE_TRACE_IRQSOFF
	/* Reset the max trace, since it contains the calibration time now. */
	rthal_trace_max_reset();
#endif /* CONFIG_IPIPE_TRACE_IRQSOFF */

	return rthal_imuldiv(dt, 20, RTHAL_CPU_FREQ);
}

static void rthal_timer_set_oneshot(int rt_mode)
{
	unsigned long flags;

	flags = rthal_critical_enter(rthal_critical_sync);
	if (rt_mode) {
		rthal_sync_op = RTHAL_SET_ONESHOT_XENOMAI;
		rthal_setup_oneshot_apic(RTHAL_APIC_TIMER_VECTOR);
	} else {
		rthal_sync_op = RTHAL_SET_ONESHOT_LINUX;
		rthal_setup_oneshot_apic(LOCAL_TIMER_VECTOR);
		/* We need to keep the timing cycle alive for the kernel. */
		rthal_trigger_irq(ipipe_apic_vector_irq(LOCAL_TIMER_VECTOR));
	}
	rthal_critical_exit(flags);
}

static void rthal_timer_set_periodic(void)
{
	unsigned long flags;

	flags = rthal_critical_enter(&rthal_critical_sync);
	rthal_sync_op = RTHAL_SET_PERIODIC;
	rthal_setup_periodic_apic(RTHAL_APIC_ICOUNT, LOCAL_TIMER_VECTOR);
	rthal_critical_exit(flags);
}

int rthal_timer_request(
	void (*tick_handler)(void),
#ifdef CONFIG_GENERIC_CLOCKEVENTS
	void (*mode_emul)(enum clock_event_mode mode,
			  struct ipipe_tick_device *tdev),
	int (*tick_emul)(unsigned long delay,
			 struct ipipe_tick_device *tdev),
#endif
	int cpu)
{
	int tickval, err;

#ifdef CONFIG_GENERIC_CLOCKEVENTS
	err = ipipe_request_tickdev("lapic", mode_emul, tick_emul, cpu);

	switch (err) {
	case CLOCK_EVT_MODE_PERIODIC:
		/* oneshot tick emulation callback won't be used, ask
		 * the caller to start an internal timer for emulating
		 * a periodic tick. */
		tickval = 1000000000UL / HZ;
		break;

	case CLOCK_EVT_MODE_ONESHOT:
		/* oneshot tick emulation */
		tickval = 1;
		break;

	case CLOCK_EVT_MODE_UNUSED:
		/* we don't need to emulate the tick at all. */
		tickval = 0;
		break;

	case CLOCK_EVT_MODE_SHUTDOWN:
		return -ENODEV;

	default:
		return err;
	}

	rthal_ktimer_saved_mode = err;
#else /* !CONFIG_GENERIC_CLOCKEVENTS */
	/*
	 * When the local APIC is enabled for kernels lacking generic
	 * support for clock events, we do not need to relay the host tick
	 * since 8254 interrupts are already flowing normally to Linux
	 * (i.e. the nucleus does not intercept them, but uses a dedicated
	 * APIC-based timer interrupt instead, i.e. RTHAL_APIC_TIMER_IPI).
	 */
	tickval = 0;
	rthal_ktimer_saved_mode = KTIMER_MODE_PERIODIC;
#endif /* !CONFIG_GENERIC_CLOCKEVENTS */

	/*
	 * The rest of the initialization should only be performed
	 * once by a single CPU.
	 */
	if (cpu > 0)
		goto out;

	rthal_timer_set_oneshot(1);

	err = rthal_irq_request(RTHAL_APIC_TIMER_IPI,
				(rthal_irq_handler_t) tick_handler, NULL, NULL);

	if (err)
		return err;

#ifndef CONFIG_GENERIC_CLOCKEVENTS
	rthal_irq_host_request(RTHAL_BCAST_TICK_IRQ,
			       &rthal_broadcast_to_local_timers,
			       "rthal_broadcast_timer",
			       &rthal_broadcast_to_local_timers);
#endif

	rthal_nmi_init(&rthal_latency_above_max);
out:
	return tickval;
}

void rthal_timer_release(int cpu)
{
#ifdef CONFIG_GENERIC_CLOCKEVENTS
	ipipe_release_tickdev(cpu);
#else
	rthal_irq_host_release(RTHAL_BCAST_TICK_IRQ,
			       &rthal_broadcast_to_local_timers);
#endif

	/*
	 * The rest of the cleanup work should only be performed once
	 * by a single CPU.
	 */
	if (cpu > 0)
		return;

	rthal_nmi_release();

	rthal_irq_release(RTHAL_APIC_TIMER_IPI);

	if (rthal_ktimer_saved_mode == KTIMER_MODE_PERIODIC)
		rthal_timer_set_periodic();
	else if (rthal_ktimer_saved_mode == KTIMER_MODE_ONESHOT)
		rthal_timer_set_oneshot(0);
}

#ifdef CONFIG_GENERIC_CLOCKEVENTS

void rthal_timer_notify_switch(enum clock_event_mode mode,
			       struct ipipe_tick_device *tdev)
{
	if (rthal_processor_id() > 0)
		/*
		 * We assume all CPUs switch the same way, so we only
		 * track mode switches from the boot CPU.
		 */
		return;

	rthal_ktimer_saved_mode = mode;
}

EXPORT_SYMBOL(rthal_timer_notify_switch);

#endif	/* CONFIG_GENERIC_CLOCKEVENTS */

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

	if (domid == RTHAL_DOMAIN_ID) {
		rthal_realtime_faults[rthal_processor_id()][event]++;

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

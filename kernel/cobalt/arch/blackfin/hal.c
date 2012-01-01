/**
 *   @ingroup hal
 *   @file
 *
 *   Adeos-based Real-Time Abstraction Layer for the Blackfin
 *   architecture.
 *
 *   Copyright (C) 2005-2006 Philippe Gerum.
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
 *
 *   Blackfin-specific HAL services.
 */
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/ipipe_tickdev.h>
#include <asm/time.h>
#include <asm/system.h>
#include <asm/atomic.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <asm/xenomai/hal.h>

enum rthal_ktimer_mode rthal_ktimer_saved_mode;

#define RTHAL_SET_ONESHOT_XENOMAI	1
#define RTHAL_SET_ONESHOT_LINUX		2
#define RTHAL_SET_PERIODIC		3

static inline void rthal_setup_oneshot_coretmr(void)
{
	bfin_write_TCNTL(TMPWR);
	CSYNC();
	bfin_write_TSCALE(TIME_SCALE - 1);
	bfin_write_TPERIOD(0);
	bfin_write_TCOUNT(0);
	CSYNC();
}

static inline void rthal_setup_periodic_coretmr(void)
{
	unsigned long tcount = ((get_cclk() / (HZ * TIME_SCALE)) - 1);

	bfin_write_TCNTL(TMPWR);
	CSYNC();
	bfin_write_TSCALE(TIME_SCALE - 1);
	bfin_write_TPERIOD(tcount);
	bfin_write_TCOUNT(tcount);
	CSYNC();
	bfin_write_TCNTL(TMPWR | TMREN | TAUTORLD);
}

static void rthal_timer_set_oneshot(int rt_mode)
{
	unsigned long flags;

	flags = ipipe_critical_enter(NULL);
	if (rt_mode)
		rthal_setup_oneshot_coretmr();
	else {
		rthal_setup_oneshot_coretmr();
		/*
		 * We need to keep the timing cycle alive for the
		 * kernel.
		 */
		ipipe_raise_irq(RTHAL_TIMER_IRQ);
	}
	ipipe_critical_exit(flags);
}

static void rthal_timer_set_periodic(void)
{
	unsigned long flags;

	flags = ipipe_critical_enter(NULL);
	rthal_setup_periodic_coretmr();
	ipipe_critical_exit(flags);
}

static int cpu_timers_requested;

int rthal_timer_request(void (*tick_handler)(void),
			void (*mode_emul)(enum clock_event_mode mode,
					  struct clock_event_device *cdev),
			int (*tick_emul)(unsigned long delay,
					 struct clock_event_device *cdev),
			int cpu)
{
	unsigned long dummy, *tmfreq = &dummy;
	int tickval, ret, res;

	if (rthal_timerfreq_arg == 0)
		tmfreq = &rthal_archdata.timer_freq;

	res = ipipe_request_tickdev("bfin_core_timer", mode_emul, tick_emul, cpu,
				    tmfreq);
	switch (res) {
	case CLOCK_EVT_MODE_PERIODIC:
		/* Oneshot tick emulation callback won't be used, ask
		 * the caller to start an internal timer for emulating
		 * a periodic tick. */
		tickval = 1000000000UL / HZ;
		break;

	case CLOCK_EVT_MODE_ONESHOT:
		/* oneshot tick emulation */
		tickval = 1;
		break;

	case CLOCK_EVT_MODE_UNUSED:
		/*
		 * We don't need to emulate the tick at all. However,
		 * we have to update the timer frequency by ourselves,
		 * and enable the CORETMR interrupt as well, since the
		 * kernel did not do it.
		 */
		tickval = 0;
		*tmfreq = get_cclk();
		ipipe_enable_irq(RTHAL_TIMER_IRQ);
		break;

	case CLOCK_EVT_MODE_SHUTDOWN:
		return -ENODEV;

	default:
		return res;
	}
	rthal_ktimer_saved_mode = res;

	/*
	 * The rest of the initialization should only be performed
	 * once by a single CPU.
	 */
	if (cpu_timers_requested++ > 0)
		goto out;

	ret = ipipe_request_irq(&rthal_archdata.domain,
				RTHAL_TIMER_IRQ,
				(ipipe_irq_handler_t)tick_handler,
				NULL, NULL);
	if (ret)
		return ret;

	rthal_timer_set_oneshot(1);

out:
	return tickval;
}

void rthal_timer_release(int cpu)
{
	ipipe_release_tickdev(cpu);

	if (--cpu_timers_requested > 0)
		return;

	ipipe_free_irq(&rthal_archdata.domain, RTHAL_TIMER_IRQ);

	if (rthal_ktimer_saved_mode == KTIMER_MODE_PERIODIC)
		rthal_timer_set_periodic();
	else if (rthal_ktimer_saved_mode == KTIMER_MODE_ONESHOT)
		rthal_timer_set_oneshot(0);
	else
		ipipe_disable_irq(RTHAL_TIMER_IRQ);
}

void rthal_timer_notify_switch(enum clock_event_mode mode,
			       struct clock_event_device *cdev)
{
	if (ipipe_processor_id() > 0)
		/*
		 * We assume all CPUs switch the same way, so we only
		 * track mode switches from the boot CPU.
		 */
		return;

	rthal_ktimer_saved_mode = mode;
}

unsigned long rthal_timer_calibrate(void)
{
	return 20;	/* 20 clock cycles */
}

void xnpod_schedule_deferred(void);

int rthal_arch_init(void)
{
	__ipipe_irq_tail_hook = (unsigned long)xnpod_schedule_deferred;

	if (rthal_clockfreq_arg == 0)
		rthal_clockfreq_arg = rthal_get_clockfreq();

	/*
	 * Timer frequency is determined later when grabbing the
	 * system timer.
	 */

	return 0;
}

void rthal_arch_cleanup(void)
{
	__ipipe_irq_tail_hook = 0;
	smp_mb();
	printk(KERN_INFO "Xenomai: hal/blackfin stopped.\n");
}

#ifndef CONFIG_SMP
EXPORT_SYMBOL_GPL(rthal_atomic_set_mask);
EXPORT_SYMBOL_GPL(rthal_atomic_clear_mask);
#endif

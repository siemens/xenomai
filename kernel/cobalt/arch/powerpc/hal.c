/**
 *   @ingroup hal
 *   @file
 *
 *   Adeos-based Real-Time Abstraction Layer for PowerPC.
 *
 *   Copyright (C) 2004-2006 Philippe Gerum.
 *
 *   64-bit PowerPC adoption
 *     copyright (C) 2005 Taneli Vähäkangas and Heikki Lindholm
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
 *   PowerPC-specific HAL services.
 */
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/console.h>
#include <linux/ipipe_tickdev.h>
#include <asm/system.h>
#include <asm/hardirq.h>
#include <asm/hw_irq.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <asm/xenomai/hal.h>
#include <stdarg.h>

#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

static volatile int sync_op;

enum rthal_ktimer_mode rthal_ktimer_saved_mode;

#define RTHAL_SET_ONESHOT_XENOMAI	1
#define RTHAL_SET_ONESHOT_LINUX		2
#define RTHAL_SET_PERIODIC		3

static inline void rthal_disarm_decr(int disarmed)
{
	per_cpu(disarm_decr, ipipe_processor_id()) = disarmed;
}

static inline void rthal_setup_oneshot_dec(void)
{
#ifdef CONFIG_40x
	mtspr(SPRN_TCR, mfspr(SPRN_TCR) & ~TCR_ARE);    /* Auto-reload off. */
#endif /* CONFIG_40x */
}

static inline void rthal_setup_periodic_dec(void)
{
#ifdef CONFIG_40x
	mtspr(SPRN_TCR, mfspr(SPRN_TCR) | TCR_ARE); /* Auto-reload on. */
	mtspr(SPRN_PIT, tb_ticks_per_jiffy);
#else /* !CONFIG_40x */
	set_dec(tb_ticks_per_jiffy);
#endif /* CONFIG_40x */
}


static void critical_sync(void)
{
#ifdef CONFIG_SMP
	switch (sync_op) {
	case RTHAL_SET_ONESHOT_XENOMAI:
		rthal_setup_oneshot_dec();
		rthal_disarm_decr(1);
		break;

	case RTHAL_SET_ONESHOT_LINUX:
		rthal_setup_oneshot_dec();
		rthal_disarm_decr(0);
		/* We need to keep the timing cycle alive for the kernel. */
		ipipe_raise_irq(RTHAL_TIMER_IRQ);
		break;

	case RTHAL_SET_PERIODIC:
		rthal_setup_periodic_dec();
		rthal_disarm_decr(0);
		break;
	}
#endif
}

static void rthal_timer_set_oneshot(int rt_mode)
{
	unsigned long flags;

	flags = ipipe_critical_enter(critical_sync);
	if (rt_mode) {
		sync_op = RTHAL_SET_ONESHOT_XENOMAI;
		rthal_setup_oneshot_dec();
		rthal_disarm_decr(1);
	} else {
		sync_op = RTHAL_SET_ONESHOT_LINUX;
		rthal_setup_oneshot_dec();
		rthal_disarm_decr(0);
		/* We need to keep the timing cycle alive for the kernel. */
		ipipe_raise_irq(RTHAL_TIMER_IRQ);
	}
	ipipe_critical_exit(flags);
}

static void rthal_timer_set_periodic(void)
{
	unsigned long flags;

	flags = ipipe_critical_enter(critical_sync);
	sync_op = RTHAL_SET_PERIODIC;
	rthal_setup_periodic_dec();
	rthal_disarm_decr(0);
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

	res = ipipe_request_tickdev("decrementer", mode_emul, tick_emul, cpu,
				    tmfreq);
	switch (res) {
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

#ifdef CONFIG_SMP
	ret = ipipe_request_irq(&rthal_archdata.domain,
				RTHAL_TIMER_IPI,
				(ipipe_irq_handler_t)tick_handler,
				NULL, NULL);
	if (ret)
		return ret;
#endif

	rthal_timer_set_oneshot(1);
out:
	return tickval;
}

void rthal_timer_release(int cpu)
{
	ipipe_release_tickdev(cpu);

	if (--cpu_timers_requested > 0)
		return;

#ifdef CONFIG_SMP
	ipipe_free_irq(&rthal_archdata.domain, RTHAL_TIMER_IPI);
#endif /* CONFIG_SMP */
	ipipe_free_irq(&rthal_archdata.domain, RTHAL_TIMER_IRQ);

	if (rthal_ktimer_saved_mode == KTIMER_MODE_PERIODIC)
		rthal_timer_set_periodic();
	else if (rthal_ktimer_saved_mode == KTIMER_MODE_ONESHOT)
		rthal_timer_set_oneshot(0);
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
EXPORT_SYMBOL_GPL(rthal_timer_notify_switch);

unsigned long rthal_timer_calibrate(void)
{
	return 1000000000 / RTHAL_CLOCK_FREQ;
}

int rthal_arch_init(void)
{
#ifdef CONFIG_ALTIVEC
	if (!cpu_has_feature(CPU_FTR_ALTIVEC)) {
		printk
			("Xenomai: ALTIVEC support enabled in kernel but no hardware found.\n"
			 "         Disable CONFIG_ALTIVEC in the kernel configuration.\n");
		return -ENODEV;
	}
#endif /* CONFIG_ALTIVEC */

	if (rthal_timerfreq_arg == 0)
		rthal_timerfreq_arg = (unsigned long)rthal_get_timerfreq();

	if (rthal_clockfreq_arg == 0)
		rthal_clockfreq_arg = (unsigned long)rthal_get_clockfreq();

	return 0;
}

void rthal_arch_cleanup(void)
{
	/* Nothing to cleanup so far. */
	printk(KERN_INFO "Xenomai: hal/powerpc stopped.\n");
}

EXPORT_SYMBOL_GPL(rthal_arch_init);
EXPORT_SYMBOL_GPL(rthal_arch_cleanup);
EXPORT_SYMBOL_GPL(rthal_thread_switch);
EXPORT_SYMBOL_GPL(rthal_thread_trampoline);

#ifdef CONFIG_XENO_HW_FPU
EXPORT_SYMBOL_GPL(rthal_init_fpu);
EXPORT_SYMBOL_GPL(rthal_save_fpu);
EXPORT_SYMBOL_GPL(rthal_restore_fpu);
#endif /* CONFIG_XENO_HW_FPU */

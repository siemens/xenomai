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
 */

/**
 * @addtogroup hal
 *
 * Blackfin-specific HAL services.
 *
 *@{*/

#include <linux/version.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <asm/time.h>
#include <asm/atomic.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <asm/xenomai/hal.h>

static struct {
	unsigned long flags;
	int count;
} rthal_linux_irq[IPIPE_NR_XIRQS];

enum rthal_ktimer_mode rthal_ktimer_saved_mode;

#ifdef CONFIG_IPIPE_CORE

#define rthal_tickdev_select() \
	wrap_select_timers(&rthal_supported_cpus)

#define rthal_tickdev_unselect() \
	ipipe_timers_release()

int rthal_timer_request(void (*tick_handler)(void),
			  void (*mode_emul)(enum clock_event_mode mode,
					    struct clock_event_device *cdev),
			  int (*tick_emul)(unsigned long delay,
					   struct clock_event_device *cdev),
			  int cpu)
{
	int ret, tickval;

	ret = ipipe_timer_start(tick_handler, mode_emul, tick_emul, cpu);

	switch (ret) {
#ifdef CONFIG_GENERIC_CLOCKEVENTS
	case CLOCK_EVT_MODE_PERIODIC:
		/*
		 * Oneshot tick emulation callback won't be used, ask
		 * the caller to start an internal timer for emulating
		 * a periodic tick.
		 */
		tickval = 1000000000UL / HZ;
		break;

	case CLOCK_EVT_MODE_ONESHOT:
		/* Oneshot tick emulation */
		tickval = 1;
		break;

	case CLOCK_EVT_MODE_UNUSED:
		/* We don't need to emulate the tick at all. */
		tickval = 0;
		break;

	case CLOCK_EVT_MODE_SHUTDOWN:
		return -ENODEV;
#else /* !CONFIG_GENERIC_CLOCKEVENTS */
	case 0:
		/* We don't need to emulate the tick at all. */
		tickval = 0;
		break;
#endif /* !CONFIG_GENERIC_CLOCKEVENTS */

	default:
		return ret;
	}

	rthal_ktimer_saved_mode = ret;

	return tickval;
}

void rthal_timer_release(int cpu)
{
	ipipe_timer_stop(cpu);
}

#else /* !I-pipe core */

static int cpu_timers_requested;

#define RTHAL_SET_ONESHOT_XENOMAI	1
#define RTHAL_SET_ONESHOT_LINUX		2
#define RTHAL_SET_PERIODIC		3

/* Acknowledge the core timer IRQ. This routine does nothing, except
   preventing Linux to mask the IRQ. */

#if IPIPE_MAJOR_NUMBER < 2 && IPIPE_MINOR_NUMBER < 8
static int rthal_timer_ack(unsigned irq)
{
	return 1;
}
#else
#define rthal_timer_ack NULL
#endif

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

	flags = rthal_critical_enter(NULL);
	if (rt_mode) {
		rthal_sync_op = RTHAL_SET_ONESHOT_XENOMAI;
		rthal_setup_oneshot_coretmr();
	} else {
		rthal_sync_op = RTHAL_SET_ONESHOT_LINUX;
		rthal_setup_oneshot_coretmr();
		/* We need to keep the timing cycle alive for the kernel. */
		rthal_trigger_irq(RTHAL_TIMER_IRQ);
	}
	rthal_critical_exit(flags);
}

static void rthal_timer_set_periodic(void)
{
	unsigned long flags;

	flags = rthal_critical_enter(NULL);
	rthal_sync_op = RTHAL_SET_PERIODIC;
	rthal_setup_periodic_coretmr();
	rthal_critical_exit(flags);
}

#define rthal_tickdev_select() (0)

#define rthal_tickdev_unselect() do { } while (0)

#ifdef CONFIG_GENERIC_CLOCKEVENTS

int rthal_timer_request(
	void (*tick_handler)(void),
	void (*mode_emul)(enum clock_event_mode mode,
			  struct clock_event_device *cdev),
	int (*tick_emul)(unsigned long delay,
			 struct clock_event_device *cdev),
	int cpu)
{
	unsigned long dummy, *tmfreq = &dummy;
	int tickval, err, res;

	res = ipipe_request_tickdev("bfin_core_timer",
				    mode_emul, tick_emul, cpu, tmfreq);
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
		rthal_irq_enable(RTHAL_TIMER_IRQ);
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

	err = rthal_irq_request(RTHAL_TIMER_IRQ,
				(rthal_irq_handler_t) tick_handler,
				NULL, NULL);
	if (err)
		return err;

	rthal_timer_set_oneshot(1);

out:
	return tickval;
}

void rthal_timer_release(int cpu)
{
	ipipe_release_tickdev(cpu);

	if (--cpu_timers_requested > 0)
		return;

	rthal_irq_release(RTHAL_TIMER_IRQ);

	if (rthal_ktimer_saved_mode == KTIMER_MODE_PERIODIC)
		rthal_timer_set_periodic();
	else if (rthal_ktimer_saved_mode == KTIMER_MODE_ONESHOT)
		rthal_timer_set_oneshot(0);
	else
		rthal_irq_disable(RTHAL_TIMER_IRQ);
}

#else /* !CONFIG_GENERIC_CLOCKEVENTS */
/*
 * We never override the system tick when the generic clock event
 * framework is not available, since the I-Pipe always makes the core
 * timer exclusively available to us in such case, unconditionally
 * moving the kernel tick source to GPTMR0.
 */
int rthal_timer_request(
	void (*tick_handler)(void),
	void (*mode_emul)(enum clock_event_mode mode,
			  struct clock_event_device *cdev),
	int (*tick_emul)(unsigned long delay,
			 struct clock_event_device *cdev),
	int cpu)
{
	int err;

	if (cpu_timers_requested++ > 0)
		return 0;

	rthal_ktimer_saved_mode = KTIMER_MODE_PERIODIC;

	if (rthal_timerfreq_arg == 0)
		rthal_tunables.timer_freq = get_cclk();

	err = rthal_irq_request(RTHAL_TIMER_IRQ,
				(rthal_irq_handler_t)tick_handler,
				rthal_timer_ack, NULL);
	if (err)
		return err;

	rthal_timer_set_oneshot(1);
	rthal_irq_enable(RTHAL_TIMER_IRQ);

	return 0;
}

void rthal_timer_release(int cpu)
{
	if (--cpu_timers_requested > 0)
		return;

	rthal_irq_disable(RTHAL_TIMER_IRQ);
	rthal_irq_release(RTHAL_TIMER_IRQ);
	rthal_timer_set_periodic();
}

#endif /* !CONFIG_GENERIC_CLOCKEVENTS */

#endif /* !I-pipe core */

#ifdef CONFIG_GENERIC_CLOCKEVENTS
void rthal_timer_notify_switch(enum clock_event_mode mode,
			       struct clock_event_device *cdev)
{
	if (rthal_processor_id() > 0)
		/*
		 * We assume all CPUs switch the same way, so we only
		 * track mode switches from the boot CPU.
		 */
		return;

	rthal_ktimer_saved_mode = mode;
}

EXPORT_SYMBOL_GPL(rthal_timer_notify_switch);
#endif /* CONFIG_GENERIC_CLOCKEVENTS */

unsigned long rthal_timer_calibrate(void)
{
	return (1000000000 / RTHAL_CLOCK_FREQ) * 100;	/* 100 CPU cycles -- FIXME */
}

int rthal_irq_enable(unsigned irq)
{
	if (irq >= IPIPE_NR_XIRQS || rthal_irq_descp(irq) == NULL)
		return -EINVAL;

	return rthal_irq_chip_enable(irq);
}

int rthal_irq_disable(unsigned irq)
{

	if (irq >= IPIPE_NR_XIRQS || rthal_irq_descp(irq) == NULL)
		return -EINVAL;

	return rthal_irq_chip_disable(irq);
}

int rthal_irq_end(unsigned irq)
{
	if (irq >= IPIPE_NR_XIRQS || rthal_irq_descp(irq) == NULL)
		return -EINVAL;

	return rthal_irq_chip_end(irq);
}

int rthal_irq_host_request(unsigned irq,
			   rthal_irq_host_handler_t handler,
			   char *name, void *dev_id)
{
	unsigned long flags;

	if (irq >= IPIPE_NR_XIRQS ||
	    handler == NULL ||
	    rthal_irq_descp(irq) == NULL)
		return -EINVAL;

	rthal_irqdesc_lock(irq, flags);

	if (rthal_linux_irq[irq].count++ == 0 && rthal_irq_descp(irq)->action) {
		rthal_linux_irq[irq].flags =
		    rthal_irq_descp(irq)->action->flags;
		rthal_irq_descp(irq)->action->flags |= IRQF_SHARED;
	}

	rthal_irqdesc_unlock(irq, flags);

	return request_irq(irq, handler, IRQF_SHARED, name, dev_id);
}

int rthal_irq_host_release(unsigned irq, void *dev_id)
{
	unsigned long flags;

	if (irq >= IPIPE_NR_XIRQS ||
	    rthal_linux_irq[irq].count == 0 ||
	    rthal_irq_descp(irq) == NULL)
		return -EINVAL;

	free_irq(irq, dev_id);

	rthal_irqdesc_lock(irq, flags);

	if (--rthal_linux_irq[irq].count == 0 && rthal_irq_descp(irq)->action)
		rthal_irq_descp(irq)->action->flags =
		    rthal_linux_irq[irq].flags;

	rthal_irqdesc_unlock(irq, flags);

	return 0;
}

static inline
int do_exception_event(unsigned event, rthal_pipeline_stage_t *stage,
		       void *data)
{
	if (stage == &rthal_domain) {
		rthal_realtime_faults[rthal_processor_id()][event]++;

		if (rthal_trap_handler != NULL &&
		    rthal_trap_handler(event, stage, data) != 0)
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

	printk(KERN_INFO "Xenomai: hal/blackfin started.\n");
}

RTHAL_DECLARE_DOMAIN(rthal_domain_entry);

int rthal_arch_init(void)
{
	int ret = rthal_tickdev_select();
	if (ret < 0)
		return ret;

	if (rthal_cpufreq_arg == 0)
		rthal_cpufreq_arg = rthal_get_cpufreq();

	if (rthal_timerfreq_arg == 0)
		rthal_timerfreq_arg = rthal_get_timerfreq();

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
	rthal_tickdev_unselect();
	printk(KERN_INFO "Xenomai: hal/blackfin stopped.\n");
}

/*@}*/

EXPORT_SYMBOL_GPL(rthal_arch_init);
EXPORT_SYMBOL_GPL(rthal_arch_cleanup);
EXPORT_SYMBOL_GPL(rthal_thread_switch);
EXPORT_SYMBOL_GPL(rthal_thread_trampoline);
EXPORT_SYMBOL_GPL(rthal_defer_switch_p);
#ifndef CONFIG_SMP
EXPORT_SYMBOL_GPL(rthal_atomic_set_mask);
EXPORT_SYMBOL_GPL(rthal_atomic_clear_mask);
#endif

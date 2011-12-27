/**
 *   @ingroup hal
 *   @file
 *
 *   Adeos-based Real-Time Abstraction Layer for the NIOS2
 *   architecture.
 *
 *   Copyright (C) 2009 Philippe Gerum.
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
 * NIOS2-specific HAL services.
 *
 *@{*/

#include <linux/version.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <asm/xenomai/hal.h>

static struct {
	unsigned long flags;
	int count;
} rthal_linux_irq[IPIPE_NR_XIRQS];

/*
 * We have a dedicated high resolution timer defined by our design
 * (na_hrtimer), which the interrupt pipeline core initialized at boot
 * up. Therefore, there is not much left to do here.
 */
int rthal_timer_request(void (*tick_handler) (void), int cpu)
{
	int ret;

	ret = rthal_irq_request(RTHAL_TIMER_IRQ,
				(rthal_irq_handler_t)tick_handler,
				NULL, NULL);
	if (ret)
		return ret;

	rthal_irq_enable(RTHAL_TIMER_IRQ);

	return 0;
}

void rthal_timer_release(int cpu)
{
	rthal_irq_disable(RTHAL_TIMER_IRQ);
	rthal_irq_release(RTHAL_TIMER_IRQ);
}

unsigned long rthal_timer_calibrate(void)
{
	unsigned long flags, freq;
	u64 t, v;
	u32 d;
	int n;

	rthal_local_irq_save_hw(flags);

	rthal_read_tsc(t);

	barrier();

	for (n = 1; n < 100; n++)
		rthal_read_tsc(v);

	rthal_local_irq_restore_hw(flags);

	d = (u32)(v - t);
	freq = rthal_get_clockfreq();

	return ((1000000000 / freq) * (d / n));
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

	for (trapnr = 0; trapnr < RTHAL_NR_FAULTS; trapnr++)
		rthal_catch_exception(trapnr, &exception_event);

	printk(KERN_INFO "Xenomai: hal/nios2 started.\n");
}

RTHAL_DECLARE_DOMAIN(rthal_domain_entry);

int rthal_arch_init(void)
{
	if (rthal_cpufreq_arg == 0)
		rthal_cpufreq_arg = (unsigned long)rthal_get_cpufreq();

	if (rthal_timerfreq_arg == 0)
		rthal_timerfreq_arg = (unsigned long)rthal_get_timerfreq();

	if (rthal_clockfreq_arg == 0)
		rthal_clockfreq_arg = (unsigned long)rthal_get_clockfreq();

	return 0;
}

void rthal_arch_cleanup(void)
{
	printk(KERN_INFO "Xenomai: hal/nios2 stopped.\n");
}

/*@}*/

EXPORT_SYMBOL_GPL(rthal_arch_init);
EXPORT_SYMBOL_GPL(rthal_arch_cleanup);
EXPORT_SYMBOL_GPL(rthal_thread_switch);
EXPORT_SYMBOL_GPL(rthal_thread_trampoline);

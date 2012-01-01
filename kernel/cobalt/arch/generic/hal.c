/**
 *   @ingroup hal
 *   @file
 *
 *   Generic Real-Time HAL.
 *   Copyright &copy; 2005 Philippe Gerum.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 *   USA; either version 2 of the License, or (at your option) any later
 *   version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/**
 * @defgroup hal HAL.
 *
 * Generic Adeos-based hardware abstraction layer.
 *
 *@{*/

#include <linux/version.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/console.h>
#include <linux/kallsyms.h>
#include <linux/bitops.h>
#include <linux/hardirq.h>
#include <linux/mm.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/xenomai/hal.h>
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif /* CONFIG_PROC_FS */
#include <stdarg.h>

MODULE_LICENSE("GPL");

unsigned long rthal_timerfreq_arg;
module_param_named(timerfreq, rthal_timerfreq_arg, ulong, 0444);

unsigned long rthal_clockfreq_arg;
module_param_named(clockfreq, rthal_clockfreq_arg, ulong, 0444);

#ifdef CONFIG_SMP
static unsigned long supported_cpus_arg = -1;
module_param_named(supported_cpus, supported_cpus_arg, ulong, 0444);
#endif /* CONFIG_SMP */

static IPIPE_DEFINE_SPINLOCK(rthal_apc_lock);

struct rthal_archdata rthal_archdata;
EXPORT_SYMBOL_GPL(rthal_archdata);

static void rthal_apc_handler(unsigned virq, void *arg)
{
	void (*handler) (void *), *cookie;
	int cpu;

	spin_lock(&rthal_apc_lock);

	cpu = ipipe_processor_id();

	/*
	 * <!> This loop is not protected against a handler becoming
	 * unavailable while processing the pending queue; the
	 * software must make sure to uninstall all apcs before
	 * eventually unloading any module that may contain apc
	 * handlers. We keep the handler affinity with the poster's
	 * CPU, so that the handler is invoked on the same CPU than
	 * the code which called rthal_apc_schedule().
	 */
	while (rthal_archdata.apc_pending[cpu]) {
		int apc = ffnz(rthal_archdata.apc_pending[cpu]);
		clear_bit(apc, &rthal_archdata.apc_pending[cpu]);
		handler = rthal_archdata.apc_table[apc].handler;
		cookie = rthal_archdata.apc_table[apc].cookie;
		rthal_archdata.apc_table[apc].hits[cpu]++;
		spin_unlock(&rthal_apc_lock);
		handler(cookie);
		spin_lock(&rthal_apc_lock);
	}

	spin_unlock(&rthal_apc_lock);
}

#ifdef CONFIG_PREEMPT_RT

/*
 * On PREEMPT_RT, we need to invoke the apc handlers over a process
 * context, so that the latter can access non-atomic kernel services
 * properly. So the Adeos virq is only used to kick a per-CPU apc
 * server process which in turns runs the apc dispatcher. A bit
 * twisted, but indeed consistent with the threaded IRQ model of
 * PREEMPT_RT.
 */
#include <linux/kthread.h>

static struct task_struct *rthal_apc_servers[NR_CPUS];

static int rthal_apc_thread(void *data)
{
	unsigned cpu = (unsigned)(unsigned long)data;

	set_cpus_allowed(current, cpumask_of_cpu(cpu));
	sigfillset(&current->blocked);
	current->flags |= PF_NOFREEZE;
	/* Use highest priority here, since some apc handlers might
	   require to run as soon as possible after the request has been
	   pended. */
	ipipe_setscheduler_root(current, SCHED_FIFO, MAX_RT_PRIO - 1);

	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		rthal_apc_handler(0, NULL);
	}

	__set_current_state(TASK_RUNNING);

	return 0;
}

void rthal_apc_kicker(unsigned virq, void *cookie)
{
	wake_up_process(rthal_apc_servers[smp_processor_id()]);
}

#define rthal_apc_trampoline rthal_apc_kicker

#else /* !CONFIG_PREEMPT_RT */

#define rthal_apc_trampoline rthal_apc_handler

#endif /* CONFIG_PREEMPT_RT */

/**
 * @fn int rthal_apc_alloc (const char *name,void (*handler)(void *cookie),void *cookie)
 *
 * @brief Allocate an APC slot.
 *
 * APC is the acronym for Asynchronous Procedure Call, a mean by which
 * activities from the Xenomai domain can schedule deferred
 * invocations of handlers to be run into the Linux domain, as soon as
 * possible when the Linux kernel gets back in control. Up to
 * BITS_PER_LONG APC slots can be active at any point in time. APC
 * support is built upon Adeos's virtual interrupt support.
 *
 * The HAL guarantees that any Linux kernel service which would be
 * callable from a regular Linux interrupt handler is also available
 * to APC handlers.
 *
 * @param name is a symbolic name identifying the APC which will get
 * reported through the /proc/xenomai/apc interface. Passing NULL to
 * create an anonymous APC is allowed.
 *
 * @param handler The address of the fault handler to call upon
 * exception condition. The handle will be passed the @a cookie value
 * unmodified.
 *
 * @param cookie A user-defined opaque cookie the HAL will pass to the
 * APC handler as its sole argument.
 *
 * @return an valid APC id. is returned upon success, or a negative
 * error code otherwise:
 *
 * - -EINVAL is returned if @a handler is invalid.
 *
 * - -EBUSY is returned if no more APC slots are available.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Linux domain context.
 */
int rthal_apc_alloc(const char *name,
		    void (*handler) (void *cookie), void *cookie)
{
	unsigned long flags;
	int apc;

	if (handler == NULL)
		return -EINVAL;

	spin_lock_irqsave(&rthal_apc_lock, flags);

	if (rthal_archdata.apc_map == ~0) {
		apc = -EBUSY;
		goto out;
	}

	apc = ffz(rthal_archdata.apc_map);
	__set_bit(apc, &rthal_archdata.apc_map);
	rthal_archdata.apc_table[apc].handler = handler;
	rthal_archdata.apc_table[apc].cookie = cookie;
	rthal_archdata.apc_table[apc].name = name;
out:
	spin_unlock_irqrestore(&rthal_apc_lock, flags);

	return apc;
}
EXPORT_SYMBOL_GPL(rthal_apc_alloc);

/**
 * @fn int rthal_apc_free (int apc)
 *
 * @brief Releases an APC slot.
 *
 * This service deallocates an APC slot obtained by rthal_apc_alloc().
 *
 * @param apc The APC id. to release, as returned by a successful call
 * to the rthal_apc_alloc() service.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Any domain context.
 */
void rthal_apc_free(int apc)
{
	BUG_ON(apc < 0 || apc >= BITS_PER_LONG);
	clear_bit(apc, &rthal_archdata.apc_map);
	smp_mb__after_clear_bit();
}
EXPORT_SYMBOL_GPL(rthal_apc_free);

void xnpod_schedule_handler(void);

int rthal_init(void)
{
	int ret;

	ret = rthal_arch_init();
	if (ret)
		return ret;

#ifdef CONFIG_SMP
	{
		int cpu;
		cpus_clear(rthal_archdata.supported_cpus);
		for (cpu = 0; cpu < BITS_PER_LONG; cpu++)
			if (supported_cpus_arg & (1 << cpu))
				cpu_set(cpu, rthal_archdata.supported_cpus);
	}
#endif /* CONFIG_SMP */

	/*
	 * The arch-dependent support must have updated the various
	 * frequency args as required.
	 */
	if (rthal_clockfreq_arg == 0) {
		printk(KERN_ERR "Xenomai: null clock frequency? Aborting.\n");
		return -ENODEV;
	}

	rthal_archdata.timer_freq = rthal_timerfreq_arg;
	rthal_archdata.clock_freq = rthal_clockfreq_arg;

	ipipe_register_head(&rthal_archdata.domain, "Xenomai");

	rthal_archdata.apc_virq = ipipe_alloc_virq();
	BUG_ON(rthal_archdata.apc_virq == 0);
	rthal_archdata.escalate_virq = ipipe_alloc_virq();
	BUG_ON(rthal_archdata.escalate_virq == 0);

	ret = ipipe_request_irq(ipipe_root_domain,
				rthal_archdata.apc_virq,
				&rthal_apc_handler,
				NULL, NULL);
	BUG_ON(ret);

	ret = ipipe_request_irq(&rthal_archdata.domain,
				rthal_archdata.escalate_virq,
				(ipipe_irq_handler_t)xnpod_schedule_handler,
				NULL, NULL);
	BUG_ON(ret);

	return 0;
}
EXPORT_SYMBOL_GPL(rthal_init);

void rthal_exit(void)
{
	ipipe_unregister_head(&rthal_archdata.domain);
	ipipe_free_irq(ipipe_root_domain, rthal_archdata.apc_virq);
	ipipe_free_virq(rthal_archdata.apc_virq);
	ipipe_free_irq(&rthal_archdata.domain, rthal_archdata.escalate_virq);
	ipipe_free_virq(rthal_archdata.escalate_virq);
	rthal_arch_cleanup();
}
EXPORT_SYMBOL_GPL(rthal_exit);

unsigned long long __rthal_generic_full_divmod64(unsigned long long a,
						 unsigned long long b,
						 unsigned long long *rem)
{
	unsigned long long q = 0, r = a;
	int i;

	for (i = fls(a >> 32) - fls(b >> 32), b <<= i; i >= 0; i--, b >>= 1) {
		q <<= 1;
		if (b <= r) {
			r -= b;
			q++;
		}
	}

	if (rem)
		*rem = r;
	return q;
}
EXPORT_SYMBOL_GPL(__rthal_generic_full_divmod64);

/**
 * \fn int rthal_timer_request(void (*tick_handler)(void),
 *             void (*mode_emul)(enum clock_event_mode mode, struct clock_event_device *cdev),
 *             int (*tick_emul)(unsigned long delay, struct clock_event_device *cdev), int cpu)
 * \brief Grab the hardware timer.
 *
 * rthal_timer_request() grabs and tunes the hardware timer in oneshot
 * mode in order to clock the master time base. GENERIC_CLOCKEVENTS is
 * required from the host kernel.
 *
 * A user-defined routine is registered as the clock tick handler.
 * This handler will always be invoked on behalf of the Xenomai domain
 * for each incoming tick.
 *
 * Host tick emulation is a way to share the clockchip hardware
 * between Linux and Xenomai, when the former provides support for
 * oneshot timing (i.e. high resolution timers and no-HZ scheduler
 * ticking).
 *
 * @param tick_handler The address of the Xenomai tick handler which will
 * process each incoming tick.
 *
 * @param mode_emul The optional address of a callback to be invoked
 * upon mode switch of the host tick device, notified by the Linux
 * kernel.
 *
 * @param tick_emul The optional address of a callback to be invoked
 * upon setup of the next shot date for the host tick device, notified
 * by the Linux kernel.
 *
 * @param cpu The CPU number to grab the timer from.
 *
 * @return a positive value is returned on success, representing the
 * duration of a Linux periodic tick expressed as a count of
 * nanoseconds; zero should be returned when the Linux kernel does not
 * undergo periodic timing on the given CPU (e.g. oneshot
 * mode). Otherwise:
 *
 * - -EBUSY is returned if the hardware timer has already been
 * grabbed.  rthal_timer_request() must be issued before
 * rthal_timer_request() is called again.
 *
 * - -ENODEV is returned if the hardware timer cannot be used.  This
 * situation may occur after the kernel disabled the timer due to
 * invalid calibration results; in such a case, such hardware is
 * unusable for any timing duties.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Linux domain context.
 */

/**
 * \fn void rthal_timer_release(int cpu)
 * \brief Release the hardware timer.
 *
 * Releases the hardware timer, thus reverting the effect of a
 * previous call to rthal_timer_request(). In case the timer hardware
 * is shared with Linux, a periodic setup suitable for the Linux
 * kernel will be reset.
 *
 * @param cpu The CPU number the timer was grabbed from.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Linux domain context.
 */

/*@}*/

EXPORT_SYMBOL_GPL(rthal_timer_request);
EXPORT_SYMBOL_GPL(rthal_timer_release);
EXPORT_SYMBOL_GPL(rthal_timer_calibrate);

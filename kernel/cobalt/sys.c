/**
 * Copyright (C) 2001-2013 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) 2001-2013 The Xenomai project <http://www.Xenomai.org>
 *
 * Xenomai is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * @defgroup nucleus Xenomai core services.
 * @{
 */
#include <stdarg.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <cobalt/kernel/sched.h>
#include <cobalt/kernel/timer.h>
#include <cobalt/kernel/intr.h>
#include <cobalt/kernel/heap.h>
#include <cobalt/kernel/clock.h>
#include <cobalt/kernel/trace.h>
#include <cobalt/kernel/assert.h>
#include <cobalt/kernel/select.h>
#include <cobalt/kernel/shadow.h>
#include <cobalt/kernel/lock.h>
#include <cobalt/kernel/sys.h>

cpumask_t nkaffinity = CPU_MASK_ALL;

void (*nkpanic)(const char *format, ...) = panic;
EXPORT_SYMBOL_GPL(nkpanic);

static void fatal(const char *format, ...)
{
	static char msg_buf[1024];
	struct xnthread *thread;
	struct xnsched *sched;
	static int oopsed;
	char pbuf[16];
	xnticks_t now;
	unsigned cpu;
	va_list ap;
	int cprio;
	spl_t s;

	xntrace_panic_freeze();
	ipipe_prepare_panic();

	xnlock_get_irqsave(&nklock, s);

	if (oopsed)
		goto out;

	oopsed = 1;
	va_start(ap, format);
	vsnprintf(msg_buf, sizeof(msg_buf), format, ap);
	printk(XENO_ERR "%s", msg_buf);
	va_end(ap);

	now = xnclock_read_monotonic(&nkclock);

	printk(KERN_ERR "\n %-3s  %-6s %-8s %-8s %-8s  %s\n",
	       "CPU", "PID", "PRI", "TIMEOUT", "STAT", "NAME");

	/*
	 * NOTE: &nkthreadq can't be empty, we have the root thread(s)
	 * linked there at least.
	 */
	for_each_online_cpu(cpu) {
		sched = xnsched_struct(cpu);
		list_for_each_entry(thread, &nkthreadq, glink) {
			if (thread->sched != sched)
				continue;
			cprio = xnthread_current_priority(thread);
			snprintf(pbuf, sizeof(pbuf), "%3d", cprio);
			printk(KERN_ERR "%c%3u  %-6d %-8s %-8Lu %.8lx  %s\n",
			       thread == sched->curr ? '>' : ' ',
			       cpu,
			       xnthread_host_pid(thread),
			       pbuf,
			       xnthread_get_timeout(thread, now),
			       xnthread_state_flags(thread),
			       xnthread_name(thread));
		}
	}

	printk(KERN_ERR "Master time base: clock=%Lu\n",
	       xnclock_read_raw(&nkclock));
#ifdef CONFIG_SMP
	printk(KERN_ERR "Current CPU: #%d\n", ipipe_processor_id());
#endif
out:
	xnlock_put_irqrestore(&nklock, s);

	show_stack(NULL,NULL);
	xntrace_panic_dump();
	for (;;)
		cpu_relax();
}

static void flush_heap(struct xnheap *heap,
		       void *extaddr, unsigned long extsize, void *cookie)
{
	free_pages_exact(extaddr, extsize);
}

static int enable_timesource(void)
{
	struct xnsched *sched;
	int htickval, cpu;
	spl_t s;

	trace_mark(xn_nucleus, enable_timesource, MARK_NOARGS);

#ifdef CONFIG_XENO_OPT_STATS
	/*
	 * Only for statistical purpose, the timer interrupt is
	 * attached by xntimer_grab_hardware().
	 */
	xnintr_init(&nktimer, "[timer]",
		    per_cpu(ipipe_percpu.hrtimer_irq, 0), NULL, NULL, 0);
#endif /* CONFIG_XENO_OPT_STATS */

	nkclock.wallclock_offset =
		xnclock_get_host_time() - xnclock_read_monotonic(&nkclock);

	for_each_xenomai_cpu(cpu) {
		htickval = xntimer_grab_hardware(cpu);
		if (htickval < 0) {
			while (--cpu >= 0)
				xntimer_release_hardware(cpu);

			return htickval;
		}

		xnlock_get_irqsave(&nklock, s);

		/* If the current tick device for the target CPU is
		 * periodic, we won't be called back for host tick
		 * emulation. Therefore, we need to start a periodic
		 * nucleus timer which will emulate the ticking for
		 * that CPU, since we are going to hijack the hw clock
		 * chip for managing our own system timer.
		 *
		 * CAUTION:
		 *
		 * - nucleus timers may be started only _after_ the hw
		 * timer has been set up for the target CPU through a
		 * call to xntimer_grab_hardware().
		 *
		 * - we don't compensate for the elapsed portion of
		 * the current host tick, since we cannot get this
		 * information easily for all CPUs except the current
		 * one, and also because of the declining relevance of
		 * the jiffies clocksource anyway.
		 *
		 * - we must not hold the nklock across calls to
		 * xntimer_grab_hardware().
		 */

		sched = xnsched_struct(cpu);
		if (htickval > 1)
			xntimer_start(&sched->htimer, htickval, htickval, XN_RELATIVE);
		else if (htickval == 1)
			xntimer_start(&sched->htimer, 0, 0, XN_RELATIVE);

#if defined(CONFIG_XENO_OPT_WATCHDOG)
		xntimer_start(&sched->wdtimer, 1000000000UL, 1000000000UL, XN_RELATIVE);
		xnsched_reset_watchdog(sched);
#endif /* CONFIG_XENO_OPT_WATCHDOG */
		xnlock_put_irqrestore(&nklock, s);
	}

	return 0;
}

/**
 * @fn int xnsys_init(void)
 * @brief Bootstrap the Xenomai core system.
 *
 * This call runs once in the kernel's lifetime at bootup. Basically,
 * the main heap is allocated early, the scheduler is initialized and the
 * core clock event source is enabled.
 *
 * @return 0 is returned on success. Otherwise:
 *
 * - -ENOMEM is returned if the memory manager fails to initialize.
 *
 * - -ENODEV is returned if a failure occurred while configuring the
 * hardware timer.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization code
 *
 * @note On every architecture, Xenomai directly manages a hardware
 * timer clocked in one-shot mode, to support any number of software
 * timers internally. Timings are always specified as a count of
 * nanoseconds.
 *
 * enable_timesource() configures the hardware timer chip. Because
 * Xenomai often interposes on the system timer used by the Linux
 * kernel, a software timer may be started to relay ticks to the host
 * kernel if needed.
 */
int xnsys_init(void)
{
	struct xnsched *sched;
	void *heapaddr;
	int ret, cpu;

	heapaddr = alloc_pages_exact(CONFIG_XENO_OPT_SYS_HEAPSZ * 1024, GFP_KERNEL);
	if (heapaddr == NULL ||
	    xnheap_init(&kheap, heapaddr, CONFIG_XENO_OPT_SYS_HEAPSZ * 1024,
			XNHEAP_PAGE_SIZE) != 0) {
		return -ENOMEM;
	}
	xnheap_set_label(&kheap, "main heap");

	for_each_xenomai_cpu(cpu) {
		sched = &per_cpu(nksched, cpu);
		xnsched_init(sched, cpu);
	}

#ifdef CONFIG_SMP
	ipipe_request_irq(&xnarch_machdata.domain,
			  IPIPE_RESCHEDULE_IPI,
			  (ipipe_irq_handler_t)__xnsched_run_handler,
			  NULL, NULL);
#endif

	xnregistry_init();

	nkpanic = fatal;
	smp_wmb();
	xnshadow_grab_events();

	ret = enable_timesource();
	if (ret)
		xnsys_shutdown();

	return ret;
}

static void disable_timesource(void)
{
	int cpu;

	trace_mark(xn_nucleus, disable_timesource, MARK_NOARGS);

	/*
	 * We must not hold the nklock while stopping the hardware
	 * timer, since this could cause deadlock situations to arise
	 * on SMP systems.
	 */
	for_each_xenomai_cpu(cpu)
		xntimer_release_hardware(cpu);

#ifdef CONFIG_XENO_OPT_STATS
	xnintr_destroy(&nktimer);
#endif /* CONFIG_XENO_OPT_STATS */
}

/**
 * @fn void xnsys_shutdown(void)
 * @brief Shutdown the Xenomai system.
 *
 * Forcibly shutdowns the system. All existing threads (but the root
 * one) are terminated.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 */
void xnsys_shutdown(void)
{
	struct xnthread *thread, *tmp;
	struct xnsched *sched;
	int cpu;
	spl_t s;

	disable_timesource();
	xnshadow_release_events();
#ifdef CONFIG_SMP
	ipipe_free_irq(&xnarch_machdata.domain, IPIPE_RESCHEDULE_IPI);
#endif

	xnlock_get_irqsave(&nklock, s);

	/* NOTE: &nkthreadq can't be empty (root thread(s)). */
	list_for_each_entry_safe(thread, tmp, &nkthreadq, glink) {
		if (!xnthread_test_state(thread, XNROOT))
			xnthread_cancel(thread);
	}

	xnsched_run();

	for_each_online_cpu(cpu) {
		sched = xnsched_struct(cpu);
		xnsched_destroy(sched);
	}

	xnlock_put_irqrestore(&nklock, s);

	xnregistry_cleanup();
	xnheap_destroy(&kheap, flush_heap, NULL);
}
EXPORT_SYMBOL_GPL(xnsys_shutdown);

/* Xenomai's generic personality. */
struct xnpersonality xenomai_personality = {
	.name = "xenomai",
	/* .magic = 0 */
};
EXPORT_SYMBOL_GPL(xenomai_personality);

/* @} */

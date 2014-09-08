/*
 * Copyright (C) 2001-2013 Philippe Gerum <rpm@xenomai.org>.
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
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/ipipe_tickdev.h>
#include <xenomai/version.h>
#include <cobalt/kernel/sched.h>
#include <cobalt/kernel/clock.h>
#include <cobalt/kernel/timer.h>
#include <cobalt/kernel/heap.h>
#include <cobalt/kernel/intr.h>
#include <cobalt/kernel/apc.h>
#include <cobalt/kernel/ppd.h>
#include <cobalt/kernel/pipe.h>
#include <cobalt/kernel/select.h>
#include <cobalt/kernel/vdso.h>
#include <rtdm/fd.h>
#include "rtdm/internal.h"
#include "posix/internal.h"
#include "procfs.h"

/**
 * @defgroup cobalt Cobalt
 *
 * Cobalt supplements the native Linux kernel in dual kernel
 * configurations. It deals with all time-critical activities, such as
 * handling interrupts, and scheduling real-time threads. The Cobalt
 * kernel has higher priority over all the native kernel activities.
 *
 * Cobalt provides an implementation of the POSIX and RTDM interfaces
 * based on a set of generic RTOS building blocks.
 */
MODULE_DESCRIPTION("Cobalt kernel");
MODULE_AUTHOR("rpm@xenomai.org");
MODULE_LICENSE("GPL");

static unsigned long timerfreq_arg;
module_param_named(timerfreq, timerfreq_arg, ulong, 0444);

static unsigned long clockfreq_arg;
module_param_named(clockfreq, clockfreq_arg, ulong, 0444);

#ifdef CONFIG_SMP
static unsigned long supported_cpus_arg = -1;
module_param_named(supported_cpus, supported_cpus_arg, ulong, 0444);
#endif /* CONFIG_SMP */

static unsigned long sysheap_size_arg;
module_param_named(sysheap_size, sysheap_size_arg, ulong, 0444);

struct xnarch_machdata xnarch_machdata;
EXPORT_SYMBOL_GPL(xnarch_machdata);

struct xnarch_percpu_machdata xnarch_percpu_machdata;
EXPORT_PER_CPU_SYMBOL_GPL(xnarch_percpu_machdata);

int __xnsys_disabled;
module_param_named(disable, __xnsys_disabled, int, 0444);
EXPORT_SYMBOL_GPL(__xnsys_disabled);

struct xnsys_ppd __xnsys_global_ppd = {
	.exe_path = "vmlinux",
};
EXPORT_SYMBOL_GPL(__xnsys_global_ppd);

#ifdef CONFIG_XENO_OPT_DEBUG
#define boot_debug_notice "[DEBUG]"
#else
#define boot_debug_notice ""
#endif

#ifdef CONFIG_IPIPE_TRACE
#define boot_lat_trace_notice "[LTRACE]"
#else
#define boot_lat_trace_notice ""
#endif

#ifdef CONFIG_ENABLE_DEFAULT_TRACERS
#define boot_evt_trace_notice "[ETRACE]"
#else
#define boot_evt_trace_notice ""
#endif

static void disable_timesource(void)
{
	int cpu;

	/*
	 * We must not hold the nklock while stopping the hardware
	 * timer, since this could cause deadlock situations to arise
	 * on SMP systems.
	 */
	for_each_realtime_cpu(cpu)
		xntimer_release_hardware(cpu);

	xntimer_release_ipi();

#ifdef CONFIG_XENO_OPT_STATS
	xnintr_destroy(&nktimer);
#endif /* CONFIG_XENO_OPT_STATS */
}

static void flush_heap(struct xnheap *heap,
		       void *mem, u32 size, void *cookie)
{
	free_pages_exact(mem, size);
}

static void sys_shutdown(void)
{
	struct xnthread *thread, *tmp;
	struct xnsched *sched;
	int cpu;
	spl_t s;

	disable_timesource();
#ifdef CONFIG_SMP
	ipipe_free_irq(&xnsched_realtime_domain, IPIPE_RESCHEDULE_IPI);
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

static int __init mach_setup(void)
{
	struct ipipe_sysinfo sysinfo;
	int ret, virq;

	ret = ipipe_select_timers(&xnsched_realtime_cpus);
	if (ret < 0)
		return ret;

	ipipe_get_sysinfo(&sysinfo);

	if (timerfreq_arg == 0)
		timerfreq_arg = sysinfo.sys_hrtimer_freq;

	if (clockfreq_arg == 0)
		clockfreq_arg = sysinfo.sys_hrclock_freq;

	if (clockfreq_arg == 0) {
		printk(XENO_ERR "null clock frequency? Aborting.\n");
		return -ENODEV;
	}

	xnarch_machdata.timer_freq = timerfreq_arg;
	xnarch_machdata.clock_freq = clockfreq_arg;

	if (xnarch_machdesc.init) {
		ret = xnarch_machdesc.init();
		if (ret)
			return ret;
	}

	ipipe_register_head(&xnsched_realtime_domain, "Xenomai");

	ret = -EBUSY;
	virq = ipipe_alloc_virq();
	if (virq == 0)
		goto fail_apc;

	xnarch_machdata.apc_virq = virq;

	ipipe_request_irq(ipipe_root_domain,
			  xnarch_machdata.apc_virq,
			  apc_dispatch,
			  NULL, NULL);

	virq = ipipe_alloc_virq();
	if (virq == 0)
		goto fail_escalate;

	xnarch_machdata.escalate_virq = virq;

	ipipe_request_irq(&xnsched_realtime_domain,
			  xnarch_machdata.escalate_virq,
			  (ipipe_irq_handler_t)__xnsched_run_handler,
			  NULL, NULL);

	ret = xnclock_init(xnarch_machdata.clock_freq);
	if (ret)
		goto fail_clock;

	return 0;

fail_clock:
	ipipe_free_irq(&xnsched_realtime_domain,
		       xnarch_machdata.escalate_virq);
	ipipe_free_virq(xnarch_machdata.escalate_virq);
fail_escalate:
	ipipe_free_irq(ipipe_root_domain,
		       xnarch_machdata.apc_virq);
	ipipe_free_virq(xnarch_machdata.apc_virq);
fail_apc:
	ipipe_unregister_head(&xnsched_realtime_domain);

	if (xnarch_machdesc.cleanup)
		xnarch_machdesc.cleanup();

	return ret;
}

static __init void mach_cleanup(void)
{
	ipipe_unregister_head(&xnsched_realtime_domain);
	ipipe_free_irq(&xnsched_realtime_domain,
		       xnarch_machdata.escalate_virq);
	ipipe_free_virq(xnarch_machdata.escalate_virq);
	ipipe_timers_release();
	xnclock_cleanup();
}

static __init int enable_timesource(void)
{
	struct xnsched *sched;
	int ret, cpu, _cpu;
	spl_t s;

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

	ret = xntimer_setup_ipi();
	if (ret)
		return ret;

	for_each_realtime_cpu(cpu) {
		ret = xntimer_grab_hardware(cpu);
		if (ret < 0)
			goto fail;

		xnlock_get_irqsave(&nklock, s);

		/*
		 * If the current tick device for the target CPU is
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
		/* Set up timer with host tick period if valid. */
		if (ret > 1)
			xntimer_start(&sched->htimer, ret, ret, XN_RELATIVE);
		else if (ret == 1)
			xntimer_start(&sched->htimer, 0, 0, XN_RELATIVE);

#ifdef CONFIG_XENO_OPT_WATCHDOG
		xntimer_start(&sched->wdtimer, 1000000000UL, 1000000000UL, XN_RELATIVE);
		xnsched_reset_watchdog(sched);
#endif
		xnlock_put_irqrestore(&nklock, s);
	}

	return 0;
fail:
	for_each_realtime_cpu(_cpu) {
		if (_cpu == cpu)
			break;
		xnlock_get_irqsave(&nklock, s);
		sched = xnsched_struct(cpu);
		xntimer_stop(&sched->htimer);
#ifdef CONFIG_XENO_OPT_WATCHDOG
		xntimer_stop(&sched->wdtimer);
#endif
		xnlock_put_irqrestore(&nklock, s);
		xntimer_release_hardware(_cpu);
	}

	xntimer_release_ipi();

	return ret;
}

static __init int sys_init(void)
{
	struct xnsched *sched;
	void *heapaddr;
	int ret, cpu;

	if (sysheap_size_arg == 0)
		sysheap_size_arg = CONFIG_XENO_OPT_SYS_HEAPSZ;

	heapaddr = alloc_pages_exact(sysheap_size_arg * 1024, GFP_KERNEL);
	if (heapaddr == NULL ||
	    xnheap_init(&kheap, heapaddr, sysheap_size_arg * 1024)) {
		return -ENOMEM;
	}
	xnheap_set_name(&kheap, "system heap");

	for_each_online_cpu(cpu) {
		sched = &per_cpu(nksched, cpu);
		xnsched_init(sched, cpu);
	}

#ifdef CONFIG_SMP
	ipipe_request_irq(&xnsched_realtime_domain,
			  IPIPE_RESCHEDULE_IPI,
			  (ipipe_irq_handler_t)__xnsched_run_handler,
			  NULL, NULL);
#endif

	xnregistry_init();

	nkpanic = __xnsys_fatal;
	smp_wmb();

	ret = enable_timesource();
	if (ret)
		sys_shutdown();

	return ret;
}

static int __init xenomai_init(void)
{
	int ret, __maybe_unused cpu;

	if (__xnsys_disabled) {
		printk(XENO_WARN "disabled on kernel command line\n");
		return 0;
	}

#ifdef CONFIG_SMP
	cpus_clear(xnsched_realtime_cpus);
	for_each_online_cpu(cpu) {
		if (supported_cpus_arg & (1UL << cpu))
			cpu_set(cpu, xnsched_realtime_cpus);
	}
	if (cpumask_empty(&xnsched_realtime_cpus)) {
		printk(XENO_WARN "disabled via empty real-time CPU mask\n");
		__xnsys_disabled = 1;
		return 0;
	}
	nkaffinity = xnsched_realtime_cpus;
#endif /* CONFIG_SMP */

	xnsched_register_classes();

	ret = xnprocfs_init_tree();
	if (ret)
		goto fail;

	ret = mach_setup();
	if (ret)
		goto cleanup_proc;

	xnintr_mount();

	ret = xnpipe_mount();
	if (ret)
		goto cleanup_mach;

	ret = xnselect_mount();
	if (ret)
		goto cleanup_pipe;

	ret = sys_init();
	if (ret)
		goto cleanup_select;

	ret = rtdm_init();
	if (ret)
		goto cleanup_sys;

	ret = cobalt_init();
	if (ret)
		goto cleanup_rtdm;

	rtdm_fd_init();

	printk(XENO_INFO "Cobalt v%s enabled %s%s%s\n",
	       XENO_VERSION_STRING,
	       boot_debug_notice,
	       boot_lat_trace_notice,
	       boot_evt_trace_notice);

	return 0;

cleanup_rtdm:
	rtdm_cleanup();
cleanup_sys:
	sys_shutdown();
cleanup_select:
	xnselect_umount();
cleanup_pipe:
	xnpipe_umount();
cleanup_mach:
	mach_cleanup();
cleanup_proc:
	xnprocfs_cleanup_tree();
fail:
	__xnsys_disabled = 1;
	printk(XENO_ERR "init failed, code %d\n", ret);

	return ret;
}
device_initcall(xenomai_init);

/**
 * @ingroup cobalt
 * @defgroup cobalt_core Cobalt kernel
 *
 * The Cobalt core is a co-kernel which supplements the Linux kernel
 * for delivering real-time services with very low latency. It
 * implements a set of generic RTOS building blocks, which the
 * Cobalt/POSIX and Cobalt/RTDM APIs are based on.  Cobalt has higher
 * priority over the Linux kernel activities.
 *
 * @{
 *
 * @page cobalt-core-tags Dual kernel service tags
 *
 * The Cobalt kernel services may be restricted to particular calling
 * contexts, or entail specific side-effects. To describe this
 * information, each service documented by this section bears a set of
 * tags when applicable.
 *
 * The table below matches the tags used throughout the documentation
 * with the description of their meaning for the caller.
 *
 * @par
 * <b>Context tags</b>
 * <TABLE>
 * <TR><TH>Tag</TH> <TH>Context on entry</TH></TR>
 * <TR><TD>primary-only</TD>	<TD>Must be called from a Cobalt task in primary mode</TD></TR>
 * <TR><TD>coreirq-only</TD>	<TD>Must be called from a Cobalt IRQ handler</TD></TR>
 * <TR><TD>secondary-only</TD>	<TD>Must be called from a Cobalt task in secondary mode or regular Linux task</TD></TR>
 * <TR><TD>rtdm-task</TD>	<TD>Must be called from a RTDM driver task</TD></TR>
 * <TR><TD>mode-unrestricted</TD>	<TD>Must be called from a Cobalt task in either primary or secondary mode</TD></TR>
 * <TR><TD>task-unrestricted</TD>	<TD>May be called from a Cobalt or regular Linux task indifferently</TD></TR>
 * <TR><TD>unrestricted</TD>	<TD>May be called from any context previously described</TD></TR>
 * <TR><TD>atomic-entry</TD>	<TD>Caller must currently hold the big Cobalt kernel lock (nklock)</TD></TR>
 * </TABLE>
 *
 * @par
 * <b>Possible side-effects</b>
 * <TABLE>
 * <TR><TH>Tag</TH> <TH>Description</TH></TR>
 * <TR><TD>might-switch</TD>	<TD>The Cobalt kernel may switch context</TD></TR>
 * </TABLE>
 *
 * @}
 */

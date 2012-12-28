/*
 * Copyright (C) 2001-2012 Philippe Gerum <rpm@xenomai.org>.
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
 * \defgroup nucleus Xenomai nucleus.
 *
 * An abstract RTOS core.
 */
#include <linux/init.h>
#include <linux/ipipe.h>
#include <nucleus/pod.h>
#include <nucleus/clock.h>
#include <nucleus/timer.h>
#include <nucleus/heap.h>
#include <nucleus/intr.h>
#include <nucleus/apc.h>
#include <nucleus/version.h>
#include <nucleus/sys_ppd.h>
#include <nucleus/pipe.h>
#include <nucleus/select.h>
#include <nucleus/vdso.h>
#include <asm-generic/xenomai/bits/timeconv.h>
#include <asm/xenomai/calibration.h>

MODULE_DESCRIPTION("Xenomai nucleus");
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

static unsigned long disable_arg;
module_param_named(disable, disable_arg, ulong, 0444);

struct xnarch_machdata xnarch_machdata;
EXPORT_SYMBOL_GPL(xnarch_machdata);

int xeno_nucleus_status = -EINVAL;

struct xnsys_ppd __xnsys_global_ppd = {
	.exe_path = "vmlinux",
};
EXPORT_SYMBOL_GPL(__xnsys_global_ppd);

#ifdef CONFIG_XENO_OPT_DEBUG
#define boot_notice " [DEBUG]"
#else
#define boot_notice ""
#endif

static int __init mach_setup(void)
{
	int ret, virq, __maybe_unused cpu;
	struct ipipe_sysinfo sysinfo;

	if (disable_arg) {
		printk("Xenomai: disabled on kernel command line\n");
		return -ENOSYS;
	}

#ifdef CONFIG_SMP
	cpus_clear(xnarch_machdata.supported_cpus);
	for (cpu = 0; cpu < num_online_cpus(); cpu++)
		if (supported_cpus_arg & (1UL << cpu))
			cpu_set(cpu, xnarch_machdata.supported_cpus);
#endif /* CONFIG_SMP */

	ret = ipipe_select_timers(&xnarch_supported_cpus);
	if (ret < 0)
		return ret;

	ipipe_get_sysinfo(&sysinfo);

	if (timerfreq_arg == 0)
		timerfreq_arg = sysinfo.sys_hrtimer_freq;

	if (clockfreq_arg == 0)
		clockfreq_arg = sysinfo.sys_hrclock_freq;

	if (clockfreq_arg == 0) {
		printk(KERN_ERR "Xenomai: null clock frequency? Aborting.\n");
		return -ENODEV;
	}

	xnarch_machdata.timer_freq = timerfreq_arg;
	xnarch_machdata.clock_freq = clockfreq_arg;

	if (xnarch_machdesc.init) {
		ret = xnarch_machdesc.init();
		if (ret)
			return ret;
	}

	ipipe_register_head(&xnarch_machdata.domain, "Xenomai");

	ret = -EBUSY;
	virq = ipipe_alloc_virq();
	if (virq == 0)
		goto cleanup;

	xnarch_machdata.apc_virq = virq;

	virq = ipipe_alloc_virq();
	if (virq == 0)
		goto fail_virq;

	xnarch_machdata.escalate_virq = virq;

	ipipe_request_irq(&xnarch_machdata.domain,
			  xnarch_machdata.escalate_virq,
			  (ipipe_irq_handler_t)__xnpod_schedule_handler,
			  NULL, NULL);

	xnarch_init_timeconv(xnarch_machdata.clock_freq);

	return 0;

fail_virq:
	ipipe_free_virq(virq);

cleanup:
	if (xnarch_machdesc.cleanup)
		xnarch_machdesc.cleanup();

	return ret;
}

static __init void mach_cleanup(void)
{
	ipipe_unregister_head(&xnarch_machdata.domain);
	ipipe_free_irq(&xnarch_machdata.domain, xnarch_machdata.escalate_virq);
	ipipe_free_virq(xnarch_machdata.escalate_virq);
	ipipe_timers_release();
}

static int __init xenomai_init(void)
{
	int ret;

	ret = mach_setup();
	if (ret)
		goto fail;

	ret = xnapc_init();
	if (ret)
		goto cleanup_mach;

	nktimerlat = xnarch_timer_calibrate();
	nklatency = xnarch_ns_to_tsc(xnarch_get_sched_latency()) + nktimerlat;

	ret = xnheap_init_mapped(&__xnsys_global_ppd.sem_heap,
				 CONFIG_XENO_OPT_GLOBAL_SEM_HEAPSZ * 1024,
				 XNARCH_SHARED_HEAP_FLAGS);
	if (ret)
		goto cleanup_apc;

	xnheap_set_label(&__xnsys_global_ppd.sem_heap, "global sem heap");

	xnheap_init_vdso();

	xnpod_mount();
	xnintr_mount();

#ifdef CONFIG_XENO_OPT_PIPE
	ret = xnpipe_mount();
	if (ret)
		goto cleanup_proc;
#endif /* CONFIG_XENO_OPT_PIPE */

	ret = xnselect_mount();
	if (ret)
		goto cleanup_pipe;

	ret = xnshadow_mount();
	if (ret)
		goto cleanup_select;

	ret = xnheap_mount();
	if (ret)
		goto cleanup_shadow;

	printk(XENO_INFO "Cobalt v%s enabled%s\n", XENO_VERSION_STRING, boot_notice);

	xeno_nucleus_status = 0;

	cpus_and(nkaffinity, nkaffinity, xnarch_supported_cpus);

	return 0;

cleanup_shadow:
	xnshadow_cleanup();

cleanup_select:
	xnselect_umount();

cleanup_pipe:

#ifdef CONFIG_XENO_OPT_PIPE
	xnpipe_umount();

cleanup_proc:

#endif /* CONFIG_XENO_OPT_PIPE */

	xnpod_umount();

cleanup_apc:
	xnapc_cleanup();

cleanup_mach:
	mach_cleanup();
fail:

	printk(XENO_ERR "system init failed, code %d\n", ret);

	xeno_nucleus_status = ret;

	return ret;
}
__initcall(xenomai_init);

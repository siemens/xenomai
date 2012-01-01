/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
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
#include <nucleus/module.h>
#include <nucleus/pod.h>
#include <nucleus/timer.h>
#include <nucleus/heap.h>
#include <nucleus/intr.h>
#include <nucleus/version.h>
#include <nucleus/sys_ppd.h>
#ifdef CONFIG_XENO_OPT_PIPE
#include <nucleus/pipe.h>
#endif /* CONFIG_XENO_OPT_PIPE */
#include <nucleus/select.h>
#include <nucleus/vdso.h>
#include <asm/xenomai/calibration.h>
#include <asm-generic/xenomai/bits/timeconv.h>

MODULE_DESCRIPTION("Xenomai nucleus");
MODULE_AUTHOR("rpm@xenomai.org");
MODULE_LICENSE("GPL");

u_long sysheap_size_arg = CONFIG_XENO_OPT_SYS_HEAPSZ;
module_param_named(sysheap_size, sysheap_size_arg, ulong, 0444);
MODULE_PARM_DESC(sysheap_size, "System heap size (Kb)");

u_long xnmod_sysheap_size;

int xeno_nucleus_status = -EINVAL;

struct xnsys_ppd __xnsys_global_ppd;
EXPORT_SYMBOL_GPL(__xnsys_global_ppd);

#ifdef CONFIG_XENO_OPT_DEBUG
#define boot_notice " [DEBUG]"
#else
#define boot_notice ""
#endif

int __init xenomai_init(void)
{
	int ret;

	xnmod_sysheap_size = module_param_value(sysheap_size_arg) * 1024;

	ret = rthal_init();
	if (ret)
		goto fail;

	xnarch_init_timeconv(RTHAL_CLOCK_FREQ);
	nktimerlat = rthal_timer_calibrate();
	nklatency = xnarch_ns_to_tsc(xnarch_get_sched_latency()) + nktimerlat;

	ret = xnheap_init_mapped(&__xnsys_global_ppd.sem_heap,
				 CONFIG_XENO_OPT_GLOBAL_SEM_HEAPSZ * 1024,
				 XNARCH_SHARED_HEAP_FLAGS);
	if (ret)
		goto cleanup_arch;

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

	xnloginfo("Xenomai/cobalt v%s enabled%s\n",
		  XENO_VERSION_STRING, boot_notice);

	xeno_nucleus_status = 0;

	xnarch_cpus_and(nkaffinity, nkaffinity, xnarch_supported_cpus);

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

cleanup_arch:
	rthal_exit();
fail:

	xnlogerr("system init failed, code %d.\n", ret);

	xeno_nucleus_status = ret;

	return ret;
}
__initcall(xenomai_init);

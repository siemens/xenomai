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
 */

/*!
 * \defgroup nucleus Xenomai nucleus.
 *
 * An abstract RTOS core.
 */

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
#include <asm/xenomai/bits/init.h>
#include <asm/xenomai/hal.h>
#include <nucleus/vdso.h>

#ifndef CONFIG_XENO_OPT_PERVASIVE
/*
 * We need an instance of struct xnvdso even in the non-pervasive case,
 * for example to implement hostrt support.
 */
static struct xnvdso static_nkvdso;
struct xnvdso *nkvdso;
EXPORT_SYMBOL_GPL(nkvdso);

void __init xnheap_init_vdso(void)
{
	static_nkvdso.features = XNVDSO_FEATURES;
	nkvdso = &static_nkvdso;
}
#endif /* !CONFIG_XENO_OPT_PERVASIVE */

#ifdef CONFIG_XENO_OPT_HOSTRT
static IPIPE_DEFINE_SPINLOCK(__hostrtlock);

static inline void do_hostrt_event(struct xnarch_hostrt_data *hostrt)
{
	unsigned long flags;

	/*
	 * The locking strategy is twofold:
	 * - The spinlock protects against concurrent updates from within the
	 *   Linux kernel and against preemption by Xenomai
	 * - The sequence counter is for lockless read-only access.
	 */

	spin_lock_irqsave(&__hostrtlock, flags);
	xnwrite_seqcount_begin(&nkvdso->hostrt_data.seqcount);

	nkvdso->hostrt_data.live = 1;
	nkvdso->hostrt_data.cycle_last = hostrt->cycle_last;
	nkvdso->hostrt_data.mask = hostrt->mask;
	nkvdso->hostrt_data.mult = hostrt->mult;
	nkvdso->hostrt_data.shift = hostrt->shift;
	nkvdso->hostrt_data.wall_time_sec = hostrt->wall_time_sec;
	nkvdso->hostrt_data.wall_time_nsec = hostrt->wall_time_nsec;
	nkvdso->hostrt_data.wall_to_monotonic = hostrt->wall_to_monotonic;

	xnwrite_seqcount_end(&nkvdso->hostrt_data.seqcount);
	spin_unlock_irqrestore(&__hostrtlock, flags);
}

RTHAL_DECLARE_HOSTRT_EVENT(hostrt_event);

static inline void init_hostrt(void)
{
	xnseqcount_init(&nkvdso->hostrt_data.seqcount);
	nkvdso->hostrt_data.live = 0;
	rthal_catch_hostrt(&hostrt_event);
}

static inline void cleanup_hostrt(void)
{
	rthal_catch_hostrt(NULL);
}
#else
static inline void init_hostrt(void)
{
}

static inline void cleanup_hostrt(void)
{
}
#endif /* CONFIG_XENO_OPT_HOSTRT */

MODULE_DESCRIPTION("Xenomai nucleus");
MODULE_AUTHOR("rpm@xenomai.org");
MODULE_LICENSE("GPL");

u_long sysheap_size_arg = CONFIG_XENO_OPT_SYS_HEAPSZ;
module_param_named(sysheap_size, sysheap_size_arg, ulong, 0444);
MODULE_PARM_DESC(sysheap_size, "System heap size (Kb)");

xnqueue_t xnmod_glink_queue;
EXPORT_SYMBOL_GPL(xnmod_glink_queue);

u_long xnmod_sysheap_size;

int xeno_nucleus_status = -EINVAL;

struct xnsys_ppd __xnsys_global_ppd;
EXPORT_SYMBOL_GPL(__xnsys_global_ppd);

void xnmod_alloc_glinks(xnqueue_t *freehq)
{
	xngholder_t *sholder, *eholder;

	sholder = xnheap_alloc(&kheap,
			       sizeof(xngholder_t) * XNMOD_GHOLDER_REALLOC);

	if (!sholder) {
		/* If we are running out of memory but still have some free
		   holders, just return silently, hoping that the contention
		   will disappear before we have no other choice than
		   allocating memory eventually. Otherwise, we have to raise a
		   fatal error right now. */

		if (emptyq_p(freehq))
			xnpod_fatal("cannot allocate generic holders");

		return;
	}

	for (eholder = sholder + XNMOD_GHOLDER_REALLOC;
	     sholder < eholder; sholder++) {
		inith(&sholder->glink.plink);
		appendq(freehq, &sholder->glink.plink);
	}
}
EXPORT_SYMBOL_GPL(xnmod_alloc_glinks);

int __init __xeno_sys_init(void)
{
	int ret;

	xnmod_sysheap_size = module_param_value(sysheap_size_arg) * 1024;

	ret = xnarch_init();
	if (ret)
		goto fail;

#ifndef __XENO_SIM__
	ret = xnheap_init_mapped(&__xnsys_global_ppd.sem_heap,
				 CONFIG_XENO_OPT_GLOBAL_SEM_HEAPSZ * 1024,
				 XNARCH_SHARED_HEAP_FLAGS);
	if (ret)
		goto cleanup_arch;

	xnheap_set_label(&__xnsys_global_ppd.sem_heap, "global sem heap");

	xnheap_init_vdso();
	init_hostrt();
#endif /* !__XENO_SIM__ */

#ifdef __KERNEL__
	xnpod_mount();
	xnintr_mount();

#ifdef CONFIG_XENO_OPT_PIPE
	ret = xnpipe_mount();
	if (ret)
		goto cleanup_proc;
#endif /* CONFIG_XENO_OPT_PIPE */

#ifdef CONFIG_XENO_OPT_SELECT
	ret = xnselect_mount();
	if (ret)
		goto cleanup_pipe;
#endif /* CONFIG_XENO_OPT_SELECT */

#ifdef CONFIG_XENO_OPT_PERVASIVE
	ret = xnshadow_mount();
	if (ret)
		goto cleanup_select;

	ret = xnheap_mount();
	if (ret)
		goto cleanup_shadow;
#endif /* CONFIG_XENO_OPT_PERVASIVE */
#endif /* __KERNEL__ */

	xntbase_mount();

	xnloginfo("real-time nucleus v%s (%s) loaded.\n",
		  XENO_VERSION_STRING, XENO_VERSION_NAME);

#ifdef CONFIG_XENO_OPT_DEBUG
	xnloginfo("debug mode enabled.\n");
#endif

	initq(&xnmod_glink_queue);

	xeno_nucleus_status = 0;

	xnarch_cpus_and(nkaffinity, nkaffinity, xnarch_supported_cpus);

	return 0;

#ifdef __KERNEL__

#ifdef CONFIG_XENO_OPT_PERVASIVE

      cleanup_shadow:

	xnshadow_cleanup();

      cleanup_select:
#endif /* CONFIG_XENO_OPT_PERVASIVE */

#ifdef CONFIG_XENO_OPT_SELECT
	xnselect_umount();

      cleanup_pipe:
#endif /* CONFIG_XENO_OPT_SELECT */

#ifdef CONFIG_XENO_OPT_PIPE
	xnpipe_umount();

      cleanup_proc:

#endif /* CONFIG_XENO_OPT_PIPE */

	xnpod_umount();

	cleanup_hostrt();

  cleanup_arch:
	xnarch_exit();

#endif /* __KERNEL__ */

      fail:

	xnlogerr("system init failed, code %d.\n", ret);

	xeno_nucleus_status = ret;

	return ret;
}

void __exit __xeno_sys_exit(void)
{
	xnpod_shutdown(XNPOD_NORMAL_EXIT);

#ifdef CONFIG_XENO_OPT_PERVASIVE
	/* Must take place before xnpod_umount(). */
	xnshadow_cleanup();
#endif /* CONFIG_XENO_OPT_PERVASIVE */

	xntbase_umount();
	xnpod_umount();
	cleanup_hostrt();
	xnarch_exit();

#ifdef __KERNEL__
#ifdef CONFIG_XENO_OPT_PERVASIVE
	xnheap_umount();
#endif /* CONFIG_XENO_OPT_PERVASIVE */
#ifdef CONFIG_XENO_OPT_PIPE
	xnpipe_umount();
#endif /* CONFIG_XENO_OPT_PIPE */
#endif /* __KERNEL__ */

#ifndef __XENO_SIM__
	xnheap_destroy_mapped(&__xnsys_global_ppd.sem_heap, NULL, NULL);
#endif

	xnloginfo("real-time nucleus unloaded.\n");
}

module_init(__xeno_sys_init);
module_exit(__xeno_sys_exit);

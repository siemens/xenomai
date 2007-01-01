/*
 * Copyright (C) 2003,2004 Philippe Gerum <rpm@xenomai.org>.
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

#include <nucleus/pod.h>
#include <nucleus/core.h>

static xnpod_t __core_pod;

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)

static int xncore_unload_hook(void)
{
	/* If no thread is hosted by the Xenomai pod, unload it. We are
	   called with interrupts off, nklock locked. */

	if (nkpod == &__core_pod && emptyq_p(&nkpod->threadq)) {
		xncore_umount();
		return 1;
	}

	return 0;
}

#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */

int xncore_attach(int minprio, int maxprio)
{
	int err = 0;

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
	/* We don't want to match any compatible pod, but exactely the
	   core one, so we emulate XNREUSE even more strictly here. */
	if (nkpod) {
		if (nkpod != &__core_pod)
			return -ENOSYS;
	} else {
		err = xnpod_init(&__core_pod, XNCORE_MIN_PRIO, XNCORE_MAX_PRIO, 0);

		if (err)
			return err;

		__core_pod.svctable.unload = &xncore_unload_hook;
	}
#else /* !(__KERNEL__ && CONFIG_XENO_OPT_PERVASIVE) */
	/* The skin is standalone, create a pod to attach to. */
	xnpod_t *pod = xnarch_sysalloc(sizeof(*pod));

	if (!pod)
		err = -ENOMEM;
	else {
		err =  xnpod_init(pod, minprio, maxprio, XNREUSE);
		if (err)
			xnarch_sysfree(pod, sizeof(*pod));
	}
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */

	if (!err)
		++nkpod->refcnt;

	return err;
}

void xncore_detach(int xtype)
{
	if (nkpod && --nkpod->refcnt == 1) {
		xnpod_t *pod = nkpod;
		xnpod_shutdown(xtype);
		if (pod != &__core_pod)
		    xnarch_sysfree(pod, sizeof(*pod));
	}
}

int xncore_mount(void)
{
	return 0;
}

int xncore_umount(void)
{
	if (nkpod != &__core_pod)
		return -ENOSYS;

	xnpod_shutdown(XNPOD_NORMAL_EXIT);

	return 0;
}

EXPORT_SYMBOL(xncore_attach);
EXPORT_SYMBOL(xncore_detach);

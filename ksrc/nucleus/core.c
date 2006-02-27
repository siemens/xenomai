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

static int xncore_unload_hook (void)

{
    /* If no thread is hosted by the Xenomai pod, unload it. We are
       called with interrupts off, nklock locked. */

    if (nkpod == &__core_pod && countq(&nkpod->threadq) == 0)
	{
	xncore_umount();
	return 1;
	}

    return 0;
}

int xncore_attach (void)

{
    if (nkpod)
	{
	if (nkpod != &__core_pod)
	    return -ENOSYS;

	++__core_pod.refcnt;

	return 0;
	}

    if (xnpod_init(&__core_pod,XNCORE_MIN_PRIO,XNCORE_MAX_PRIO,0) != 0)
	return -ENOSYS;

    __core_pod.svctable.unload = &xncore_unload_hook;
    __core_pod.refcnt = 1;

    return 0;
}

int xncore_detach (void)

{
    return --__core_pod.refcnt;
}

int xncore_mount (void)

{
    return 0;
}

int xncore_umount (void)

{
    if (nkpod != &__core_pod)
	return -ENOSYS;

    xnpod_shutdown(XNPOD_NORMAL_EXIT);

    return 0;
}

EXPORT_SYMBOL(xncore_attach);
EXPORT_SYMBOL(xncore_detach);

/**
 * @file
 * Real-Time Driver Model for Xenomai
 *
 * @note Copyright (C) 2005, 2006 Jan Kiszka <jan.kiszka@web.de>
 * @note Copyright (C) 2005 Joerg Langenberg <joerg.langenberg@gmx.net>
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*!
 * @ingroup rtdm
 * @defgroup profiles Device Profiles
 *
 * Device profiles define which operation handlers a driver of a certain class
 * has to implement, which name or protocol it has to register, which IOCTLs
 * it has to provide, and further details. Sub-classes can be defined in order
 * to extend a device profile with more hardware-specific functions.
 */

#include <linux/init.h>
#include <nucleus/pod.h>
#include <rtdm/syscall.h>
#include "rtdm/internal.h"

MODULE_DESCRIPTION("Real-Time Driver Model");
MODULE_AUTHOR("jan.kiszka@web.de");
MODULE_LICENSE("GPL");

static int __init __rtdm_init(void)
{
	int err;

	err = xnpod_init();
	if (err)
		goto fail;

	err = rtdm_dev_init();
	if (err)
		goto cleanup_pod;

	err = rtdm_proc_init();
	if (err)
		goto cleanup_dev;

	err = rtdm_syscall_init();
	if (err)
		goto cleanup_proc;

	rtdm_initialised = 1;

	xnprintf("starting RTDM services.\n");

	return 0;

cleanup_proc:
	rtdm_proc_cleanup();

cleanup_dev:
	rtdm_dev_cleanup();

cleanup_pod:
	xnpod_shutdown(err);

fail:
	xnlogerr("RTDM init failed, code %d.\n", err);
	return err;
}
device_initcall(__rtdm_init);

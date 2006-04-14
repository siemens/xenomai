/**
 * @file
 * Real-Time Driver Model for Xenomai
 *
 * @note Copyright (C) 2005 Jan Kiszka <jan.kiszka@web.de>
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
 * @defgroup rtdm Real-Time Driver Model
 *
 * The Real-Time Driver Model (RTDM) provides a unified interface to
 * both users and developers of real-time device
 * drivers. Specifically, it addresses the constraints of mixed
 * RT/non-RT systems like Xenomai. RTDM conforms to POSIX
 * semantics (IEEE Std 1003.1) where available and applicable.
 *
 * @b API @b Revision: 4
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

#include <nucleus/pod.h>
#ifdef __KERNEL__
#include <nucleus/core.h>
#include <rtdm/syscall.h>
#endif /* __KERNEL__ */

#include "rtdm/device.h"
#include "rtdm/proc.h"


MODULE_DESCRIPTION("Real-Time Driver Model");
MODULE_AUTHOR("jan.kiszka@web.de");
MODULE_LICENSE("GPL");

#if !defined(__KERNEL__) || !defined(CONFIG_XENO_OPT_PERVASIVE)
static xnpod_t __rtdm_pod;
#endif /* !__KERNEL__ && CONFIG_XENO_OPT_PERVASIVE) */


static void rtdm_skin_shutdown(int xtype)
{
    rtdm_dev_cleanup();

#ifdef CONFIG_PROC_FS
    rtdm_proc_cleanup();
#endif /* CONFIG_PROC_FS */

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
    rtdm_syscall_cleanup();
    xncore_detach(xtype);
#else /* !(__KERNEL__ && CONFIG_XENO_OPT_PERVASIVE) */
    xnpod_shutdown(xtype);
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */
}


int SKIN_INIT(rtdm)
{
    int err;

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
    /* The RTDM skin is stacked over the Xenomai pod. */
    err = xncore_attach();
#else /* !(__KERNEL__ && CONFIG_XENO_OPT_PERVASIVE) */
    /* The RTDM skin is standalone. */
    err = xnpod_init(&__rtdm_pod, XNCORE_LOW_PRIO, XNCORE_HIGH_PRIO, XNREUSE);
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */

    if (err)
        goto fail;

    err = rtdm_dev_init();
    if (err)
        goto cleanup_pod;

#ifdef CONFIG_PROC_FS
    err = rtdm_proc_init();
    if (err)
        goto cleanup_dev;
#endif /* CONFIG_PROC_FS */

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
    err = rtdm_syscall_init();
    if (err)
        goto cleanup_proc;
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */

    xnprintf("starting RTDM services.\n");

    return 0;

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
  cleanup_proc:
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */

#ifdef CONFIG_PROC_FS
    rtdm_proc_cleanup();

  cleanup_dev:
#endif /* CONFIG_PROC_FS */

    rtdm_dev_cleanup();

  cleanup_pod:
#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
    xncore_detach(err);
#else /* !(__KERNEL__ && CONFIG_XENO_OPT_PERVASIVE) */
    xnpod_shutdown(err);
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */

  fail:

    xnlogerr("RTDM skin init failed, code %d.\n",err);

    return err;
}

void SKIN_EXIT(rtdm)
{
    xnprintf("stopping RTDM services.\n");
    rtdm_skin_shutdown(XNPOD_NORMAL_EXIT);
}

module_init(__rtdm_skin_init);
module_exit(__rtdm_skin_exit);

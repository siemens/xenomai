/**
 *
 * @note Copyright (C) 2004 Philippe Gerum <rpm@xenomai.org> 
 * @note Copyright (C) 2005 Nextream France S.A.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <xenomai/nucleus/pod.h>
#ifdef __KERNEL__
#include <xenomai/rtai/syscall.h>
#include <xenomai/rtai/fifo.h>
#endif /* __KERNEL__ */
#include <xenomai/rtai/task.h>
#include <xenomai/rtai/sem.h>
#include <xenomai/rtai/shm.h>

MODULE_DESCRIPTION("RTAI API emulator");
MODULE_AUTHOR("rpm@xenomai.org");
MODULE_LICENSE("GPL");

#if !defined(__KERNEL__) || !defined(CONFIG_XENO_OPT_PERVASIVE)
static xnpod_t __rtai_pod;
#endif /* !__KERNEL__ && CONFIG_XENO_OPT_PERVASIVE) */

static void rtai_shutdown (int xtype)

{
#ifdef CONFIG_XENO_OPT_RTAI_SHM
    __rtai_shm_pkg_cleanup();
#endif /* CONFIG_XENO_OPT_RTAI_SHM */

#ifdef CONFIG_XENO_OPT_RTAI_FIFO
    __rtai_fifo_pkg_cleanup();
#endif /* CONFIG_XENO_OPT_RTAI_FIFO */

#ifdef CONFIG_XENO_OPT_RTAI_SEM
    __rtai_sem_pkg_cleanup();
#endif /* CONFIG_XENO_OPT_RTAI_SEM */

    __rtai_task_pkg_cleanup();

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
    __rtai_syscall_cleanup();
    xncore_detach();
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */

    xnpod_shutdown(xtype);
}

int SKIN_INIT(rtai)

{
    int err;

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
    /* The RTAI emulator is stacked over the shared Xenomai pod. */
    err = xncore_attach();
#else /* !(__KERNEL__ && CONFIG_XENO_OPT_PERVASIVE) */
    /* The RTAI emulator is standalone. */
    err = xnpod_init(&__rtai_pod,XNCORE_MIN_PRIO,XNCORE_MAX_PRIO,0);
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */

    if (err)
	goto fail;

    err = __rtai_task_pkg_init();

    if (err)
	goto cleanup_pod;

#ifdef CONFIG_XENO_OPT_RTAI_SEM
    err = __rtai_sem_pkg_init();

    if (err)
	goto cleanup_task;
#endif /* CONFIG_XENO_OPT_RTAI_SEM */

#ifdef CONFIG_XENO_OPT_RTAI_FIFO
    err = __rtai_fifo_pkg_init();

    if (err)
	goto cleanup_sem;
#endif /* CONFIG_XENO_OPT_RTAI_FIFO */

#ifdef CONFIG_XENO_OPT_RTAI_SHM
    err = __rtai_shm_pkg_init();

    if (err)
	goto cleanup_fifo;
#endif /* CONFIG_XENO_OPT_RTAI_SHM */

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
    err = __rtai_syscall_init();

    if (err)
	goto cleanup_shm;
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */
    
    xnprintf("starting RTAI emulator.\n");

    return 0;	/* SUCCESS. */

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
 cleanup_shm:
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */

#ifdef CONFIG_XENO_OPT_RTAI_SHM
    __rtai_shm_pkg_cleanup();

 cleanup_fifo:
#endif /* CONFIG_XENO_OPT_RTAI_SHM */

#ifdef CONFIG_XENO_OPT_RTAI_FIFO
    __rtai_fifo_pkg_cleanup();

 cleanup_sem:
#endif /* CONFIG_XENO_OPT_RTAI_FIFO */

#ifdef CONFIG_XENO_OPT_RTAI_SEM
    __rtai_sem_pkg_cleanup();

 cleanup_task:
#endif /* CONFIG_XENO_OPT_RTAI_SEM */

    __rtai_task_pkg_cleanup();

 cleanup_pod:

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
    __rtai_syscall_cleanup();
    xncore_detach();
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */

    xnpod_shutdown(XNPOD_NORMAL_EXIT);

 fail:

    return err;
}

void SKIN_EXIT(rtai)

{
    xnprintf("stopping RTAI emulator.\n");
    rtai_shutdown(XNPOD_NORMAL_EXIT);
}

module_init(__rtai_skin_init);
module_exit(__rtai_skin_exit);

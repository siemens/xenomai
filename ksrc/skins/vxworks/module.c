/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
 * Copyright (C) 2003 Philippe Gerum <rpm@xenomai.org>.
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

#include <vxworks/defs.h>
#ifdef __KERNEL__
#include <vxworks/syscall.h>
#endif /* __KERNEL__ */

MODULE_DESCRIPTION("VxWorks(R) virtual machine");
MODULE_AUTHOR("gilles.chanteperdrix@laposte.net");
MODULE_LICENSE("GPL");

#if !defined(__KERNEL__) || !defined(CONFIG_XENO_OPT_PERVASIVE)
static xnpod_t __vxworks_pod;
#endif /* !__KERNEL__ && CONFIG_XENO_OPT_PERVASIVE) */

#ifdef CONFIG_XENO_EXPORT_REGISTRY
xnptree_t __vxworks_ptree = {

    .dir = NULL,
    .name = "vxworks",
    .entries = 0,
};
#endif /* CONFIG_XENO_EXPORT_REGISTRY */

int SKIN_INIT(vxworks)
{
    int err;

#if CONFIG_XENO_OPT_TIMING_PERIOD == 0
    nktickdef = 1000000;	/* Defaults to 1ms. */
#endif

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
    /* The VxWorks skin is stacked over the core pod. */
    err = xncore_attach();
#else /* !(__KERNEL__ && CONFIG_XENO_OPT_PERVASIVE) */
    /* The VxWorks skin is standalone. */
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */

    if (err != 0)
        goto fail;

    if (!testbits(nkpod->status,XNTMPER))
	{
	xnlogerr("incompatible timer mode (aperiodic found, need periodic).\n");
	err = -EBUSY;	/* Cannot work in aperiodic timing mode. */
	}
    else
	err = wind_sysclk_init(1000000000 / xnpod_get_tickval());

    if (err != 0)
        {
#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
	xncore_detach(err);
#else /* !(__KERNEL__ && CONFIG_XENO_OPT_PERVASIVE) */
	xnpod_shutdown(err);
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */
 fail:
	xnlogerr("VxWorks skin init failed, code %d.\n",err);
        return err;
        }

    wind_wd_init();
    wind_task_hooks_init();
    wind_sem_init();
    wind_msgq_init();
    wind_task_init();
#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
    wind_syscall_init();
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */
    
    xnprintf("starting VxWorks services.\n");

    return 0;
}

void SKIN_EXIT(vxworks)
{
    xnprintf("stopping VxWorks services.\n");
    wind_task_cleanup();
    wind_sysclk_cleanup();
    wind_msgq_cleanup();
    wind_sem_cleanup();
    wind_wd_cleanup();
    wind_task_hooks_cleanup();
#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
    wind_syscall_cleanup();
    xncore_detach(XNPOD_NORMAL_EXIT);
#else /* !(__KERNEL__ && CONFIG_XENO_OPT_PERVASIVE) */
    xnpod_shutdown(XNPOD_NORMAL_EXIT);
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */
}

module_init(__vxworks_skin_init);
module_exit(__vxworks_skin_exit);

/* exported API : */

EXPORT_SYMBOL(wind_current_context_errno);
EXPORT_SYMBOL(printErrno);
EXPORT_SYMBOL(errnoSet);
EXPORT_SYMBOL(errnoGet);
EXPORT_SYMBOL(errnoOfTaskGet);
EXPORT_SYMBOL(errnoOfTaskSet);
EXPORT_SYMBOL(taskSpawn);
EXPORT_SYMBOL(taskInit);
EXPORT_SYMBOL(taskActivate);
EXPORT_SYMBOL(taskExit);
EXPORT_SYMBOL(taskDelete);
EXPORT_SYMBOL(taskDeleteForce);
EXPORT_SYMBOL(taskSuspend);
EXPORT_SYMBOL(taskResume);
EXPORT_SYMBOL(taskRestart);
EXPORT_SYMBOL(taskPrioritySet);
EXPORT_SYMBOL(taskPriorityGet);
EXPORT_SYMBOL(taskLock);
EXPORT_SYMBOL(taskUnlock);
EXPORT_SYMBOL(taskIdSelf);
EXPORT_SYMBOL(taskSafe);
EXPORT_SYMBOL(taskUnsafe);
EXPORT_SYMBOL(taskDelay);
EXPORT_SYMBOL(taskIdVerify);
EXPORT_SYMBOL(taskTcb);
EXPORT_SYMBOL(taskCreateHookAdd);
EXPORT_SYMBOL(taskCreateHookDelete);
EXPORT_SYMBOL(taskSwitchHookAdd);
EXPORT_SYMBOL(taskSwitchHookDelete);
EXPORT_SYMBOL(taskDeleteHookAdd);
EXPORT_SYMBOL(taskDeleteHookDelete);
EXPORT_SYMBOL(taskName);
EXPORT_SYMBOL(taskNameToId);
EXPORT_SYMBOL(taskIdDefault);
EXPORT_SYMBOL(taskIsReady);
EXPORT_SYMBOL(taskIsSuspended);
EXPORT_SYMBOL(semGive);
EXPORT_SYMBOL(semTake);
EXPORT_SYMBOL(semFlush);
EXPORT_SYMBOL(semDelete);
EXPORT_SYMBOL(semBCreate);
EXPORT_SYMBOL(semMCreate);
EXPORT_SYMBOL(semCCreate);
EXPORT_SYMBOL(wdCreate);
EXPORT_SYMBOL(wdDelete);
EXPORT_SYMBOL(wdStart);
EXPORT_SYMBOL(wdCancel);
EXPORT_SYMBOL(msgQCreate);
EXPORT_SYMBOL(msgQDelete);
EXPORT_SYMBOL(msgQNumMsgs);
EXPORT_SYMBOL(msgQReceive);
EXPORT_SYMBOL(msgQSend);
EXPORT_SYMBOL(intContext);
EXPORT_SYMBOL(intCount);
EXPORT_SYMBOL(intLevelSet);
EXPORT_SYMBOL(intLock);
EXPORT_SYMBOL(intUnlock);
EXPORT_SYMBOL(sysClkConnect);
EXPORT_SYMBOL(sysClkDisable);
EXPORT_SYMBOL(sysClkEnable);
EXPORT_SYMBOL(sysClkRateGet);
EXPORT_SYMBOL(sysClkRateSet);
EXPORT_SYMBOL(tickAnnounce);
EXPORT_SYMBOL(tickGet);
EXPORT_SYMBOL(tickSet);
EXPORT_SYMBOL(kernelTimeSlice);
EXPORT_SYMBOL(kernelVersion);

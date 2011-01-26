/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
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
MODULE_AUTHOR("gilles.chanteperdrix@xenomai.org");
MODULE_LICENSE("GPL");

static u_long tick_arg = CONFIG_XENO_OPT_VXWORKS_PERIOD;
module_param_named(tick_arg, tick_arg, ulong, 0444);
MODULE_PARM_DESC(tick_arg, "Fixed clock tick value (us)");

u_long sync_time;
module_param_named(sync_time, sync_time, ulong, 0444);
MODULE_PARM_DESC(sync_time, "Set non-zero to synchronize on master time base");

xntbase_t *wind_tbase;

wind_rholder_t __wind_global_rholder;

DEFINE_XNPTREE(__vxworks_ptree, "vxworks");

int SKIN_INIT(vxworks)
{
	int err;

	initq(&__wind_global_rholder.wdq);
	initq(&__wind_global_rholder.msgQq);
	initq(&__wind_global_rholder.semq);

	/* The following fields are unused in the global holder;
	   still, we initialize them not to leave such data in an
	   invalid state. */
	xnsynch_init(&__wind_global_rholder.wdsynch, XNSYNCH_FIFO, NULL);
	initq(&__wind_global_rholder.wdpending);
	__wind_global_rholder.wdcount = 0;

	err = xnpod_init();

	if (err != 0)
		goto fail_core;

	err = wind_sysclk_init(tick_arg * 1000);

	if (err != 0) {
		xnpod_shutdown(err);

	fail_core:
		xnlogerr("VxWorks skin init failed, code %d.\n", err);
		return err;
	}

	wind_wd_init();
	wind_task_hooks_init();
	wind_sem_init();
	wind_msgq_init();
	wind_task_init();
#ifdef CONFIG_XENO_OPT_PERVASIVE
	wind_syscall_init();
#endif /* CONFIG_XENO_OPT_PERVASIVE */

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
#ifdef CONFIG_XENO_OPT_PERVASIVE
	wind_syscall_cleanup();
#endif /* CONFIG_XENO_OPT_PERVASIVE */
	xnpod_shutdown(XNPOD_NORMAL_EXIT);
}

module_init(__vxworks_skin_init);
module_exit(__vxworks_skin_exit);

/* exported API : */

EXPORT_SYMBOL_GPL(wind_current_context_errno);
EXPORT_SYMBOL_GPL(wind_tbase);
EXPORT_SYMBOL_GPL(printErrno);
EXPORT_SYMBOL_GPL(errnoSet);
EXPORT_SYMBOL_GPL(errnoGet);
EXPORT_SYMBOL_GPL(errnoOfTaskGet);
EXPORT_SYMBOL_GPL(errnoOfTaskSet);
EXPORT_SYMBOL_GPL(taskSpawn);
EXPORT_SYMBOL_GPL(taskInit);
EXPORT_SYMBOL_GPL(taskActivate);
EXPORT_SYMBOL_GPL(taskExit);
EXPORT_SYMBOL_GPL(taskDelete);
EXPORT_SYMBOL_GPL(taskDeleteForce);
EXPORT_SYMBOL_GPL(taskSuspend);
EXPORT_SYMBOL_GPL(taskResume);
EXPORT_SYMBOL_GPL(taskRestart);
EXPORT_SYMBOL_GPL(taskPrioritySet);
EXPORT_SYMBOL_GPL(taskPriorityGet);
EXPORT_SYMBOL_GPL(taskLock);
EXPORT_SYMBOL_GPL(taskUnlock);
EXPORT_SYMBOL_GPL(taskIdSelf);
EXPORT_SYMBOL_GPL(taskSafe);
EXPORT_SYMBOL_GPL(taskUnsafe);
EXPORT_SYMBOL_GPL(taskDelay);
EXPORT_SYMBOL_GPL(taskIdVerify);
EXPORT_SYMBOL_GPL(taskTcb);
EXPORT_SYMBOL_GPL(taskCreateHookAdd);
EXPORT_SYMBOL_GPL(taskCreateHookDelete);
EXPORT_SYMBOL_GPL(taskSwitchHookAdd);
EXPORT_SYMBOL_GPL(taskSwitchHookDelete);
EXPORT_SYMBOL_GPL(taskDeleteHookAdd);
EXPORT_SYMBOL_GPL(taskDeleteHookDelete);
EXPORT_SYMBOL_GPL(taskName);
EXPORT_SYMBOL_GPL(taskNameToId);
EXPORT_SYMBOL_GPL(taskIdDefault);
EXPORT_SYMBOL_GPL(taskIsReady);
EXPORT_SYMBOL_GPL(taskIsSuspended);
EXPORT_SYMBOL_GPL(semGive);
EXPORT_SYMBOL_GPL(semTake);
EXPORT_SYMBOL_GPL(semFlush);
EXPORT_SYMBOL_GPL(semDelete);
EXPORT_SYMBOL_GPL(semBCreate);
EXPORT_SYMBOL_GPL(semMCreate);
EXPORT_SYMBOL_GPL(semCCreate);
EXPORT_SYMBOL_GPL(wdCreate);
EXPORT_SYMBOL_GPL(wdDelete);
EXPORT_SYMBOL_GPL(wdStart);
EXPORT_SYMBOL_GPL(wdCancel);
EXPORT_SYMBOL_GPL(msgQCreate);
EXPORT_SYMBOL_GPL(msgQDelete);
EXPORT_SYMBOL_GPL(msgQNumMsgs);
EXPORT_SYMBOL_GPL(msgQReceive);
EXPORT_SYMBOL_GPL(msgQSend);
EXPORT_SYMBOL_GPL(intContext);
EXPORT_SYMBOL_GPL(intCount);
EXPORT_SYMBOL_GPL(intLevelSet);
EXPORT_SYMBOL_GPL(intLock);
EXPORT_SYMBOL_GPL(intUnlock);
EXPORT_SYMBOL_GPL(sysClkConnect);
EXPORT_SYMBOL_GPL(sysClkDisable);
EXPORT_SYMBOL_GPL(sysClkEnable);
EXPORT_SYMBOL_GPL(sysClkRateGet);
EXPORT_SYMBOL_GPL(sysClkRateSet);
EXPORT_SYMBOL_GPL(tickAnnounce);
EXPORT_SYMBOL_GPL(tickGet);
EXPORT_SYMBOL_GPL(tickSet);
EXPORT_SYMBOL_GPL(kernelTimeSlice);
EXPORT_SYMBOL_GPL(kernelVersion);
EXPORT_SYMBOL_GPL(taskInfoGet);

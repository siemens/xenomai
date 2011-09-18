/*
 * Copyright (C) 2006 Philippe Gerum <rpm@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#include <sys/types.h>
#include <stdio.h>
#include <memory.h>
#include <malloc.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <nucleus/sched.h>
#include <vxworks/vxworks.h>
#include <asm-generic/bits/sigshadow.h>
#include <asm-generic/bits/current.h>
#include <asm-generic/stack.h>
#include "wrappers.h"

#ifdef HAVE___THREAD
__thread WIND_TCB
__vxworks_self __attribute__ ((tls_model ("initial-exec"))) = {
	.handle = XN_NO_HANDLE
};
#else /* !HAVE___THREAD */
extern pthread_key_t __vxworks_tskey;
#endif /* !HAVE___THREAD */

extern int __vxworks_muxid;

/* Public Xenomai interface. */

struct wind_task_iargs {
	WIND_TCB *pTcb;
	const char *name;
	int prio;
	int flags;
	FUNCPTR entry;
	long arg0;
	long arg1;
	long arg2;
	long arg3;
	long arg4;
	long arg5;
	long arg6;
	long arg7;
	long arg8;
	long arg9;
	xncompletion_t *completionp;
};

static int wind_task_set_posix_priority(int prio, struct sched_param *param)
{
	int maxpprio, pprio;

	maxpprio = sched_get_priority_max(SCHED_FIFO);

	/* We need to normalize this value first. */
	pprio = wind_normalized_prio(prio);
	if (pprio > maxpprio)
		pprio = maxpprio;

	memset(param, 0, sizeof(*param));
	param->sched_priority = pprio;

	return pprio ? SCHED_FIFO : SCHED_OTHER;
}

static void *wind_task_trampoline(void *cookie)
{
	struct wind_task_iargs *iargs =
	    (struct wind_task_iargs *)cookie, _iargs;
	volatile pthread_t tid = pthread_self();
	struct wind_arg_bulk bulk;
	unsigned long mode_offset;
	WIND_TCB *pTcb;
	long err;

	/* Backup the arg struct, it might vanish after completion. */
	memcpy(&_iargs, iargs, sizeof(_iargs));

	/* wind_task_delete requires asynchronous cancellation */
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	xeno_sigshadow_install_once();

	bulk.a1 = (u_long)iargs->name;
	bulk.a2 = (u_long)iargs->prio;
	bulk.a3 = (u_long)iargs->flags;
	bulk.a4 = (u_long)tid;
	bulk.a5 = (u_long)&mode_offset;
	pTcb = iargs->pTcb;

	if (!bulk.a5) {
		err = -ENOMEM;
		goto fail;
	}

	err = XENOMAI_SKINCALL3(__vxworks_muxid,
				__vxworks_task_init,
				&bulk, pTcb, iargs->completionp);
	if (err)
		goto fail;

	xeno_set_current();
	xeno_set_current_mode(mode_offset);

#ifdef HAVE___THREAD
	__vxworks_self = *pTcb;
#endif /* HAVE___THREAD */

	/* Wait on the barrier for the task to be started. The barrier
	   could be released in order to process Linux signals while the
	   Xenomai shadow is still dormant; in such a case, resume wait. */

	do
		err = XENOMAI_SYSCALL2(__xn_sys_barrier, NULL, NULL);
	while (err == -EINTR);

	if (!err)
		_iargs.entry(_iargs.arg0, _iargs.arg1, _iargs.arg2, _iargs.arg3,
			     _iargs.arg4, _iargs.arg5, _iargs.arg6, _iargs.arg7,
			     _iargs.arg8, _iargs.arg9);

      fail:

	return (void *)err;
}

STATUS taskInit(WIND_TCB *pTcb,
		const char *name,
		int prio,
		int flags,
		char *stack __attribute__ ((unused)),
		int stacksize,
		FUNCPTR entry,
		long arg0, long arg1, long arg2, long arg3, long arg4,
		long arg5, long arg6, long arg7, long arg8, long arg9)
{
	struct wind_task_iargs iargs;
	xncompletion_t completion;
	struct sched_param param;
	pthread_attr_t thattr;
	int err, policy;
	pthread_t thid;

	/* Migrate this thread to the Linux domain since we are about to
	   issue a series of regular kernel syscalls in order to create
	   the new Linux thread, which in turn will be mapped to a
	   VxWorks shadow. */

	XENOMAI_SYSCALL1(__xn_sys_migrate, XENOMAI_LINUX_DOMAIN);

	completion.syncflag = 0;
	completion.pid = -1;

	iargs.pTcb = pTcb;
	iargs.name = name;
	iargs.prio = prio;
	iargs.flags = flags;
	iargs.entry = entry;
	iargs.completionp = &completion;
	iargs.arg0 = arg0;
	iargs.arg1 = arg1;
	iargs.arg2 = arg2;
	iargs.arg3 = arg3;
	iargs.arg4 = arg4;
	iargs.arg5 = arg5;
	iargs.arg6 = arg6;
	iargs.arg7 = arg7;
	iargs.arg8 = arg8;
	iargs.arg9 = arg9;

	pthread_attr_init(&thattr);

	stacksize = xeno_stacksize(stacksize);

	pthread_attr_setinheritsched(&thattr, PTHREAD_EXPLICIT_SCHED);
	policy = wind_task_set_posix_priority(prio, &param);
	pthread_attr_setschedpolicy(&thattr, policy);
	pthread_attr_setschedparam(&thattr, &param);
	pthread_attr_setstacksize(&thattr, stacksize);
	pthread_attr_setdetachstate(&thattr, PTHREAD_CREATE_DETACHED);

	err = __real_pthread_create(&thid, &thattr, &wind_task_trampoline, &iargs);

	/* POSIX codes returned by internal calls do not conflict with
	   VxWorks ones, so let's use errno for passing the back
	   too. */

	if (err) {
		errno = err;
		return ERROR;
	}

	/* Wait for sync with wind_task_trampoline() */
	err = XENOMAI_SYSCALL1(__xn_sys_completion, &completion);

	if (err) {
		errno = abs(err);
		return ERROR;
	}

	return OK;
}

STATUS taskActivate(TASK_ID task_id)
{
	int err;

	err =
	    XENOMAI_SKINCALL1(__vxworks_muxid, __vxworks_task_activate,
			      task_id);
	if (err) {
		errno = abs(err);
		return ERROR;
	}

	return OK;
}

TASK_ID taskSpawn(const char *name,
		  int prio,
		  int flags,
		  int stacksize,
		  FUNCPTR entry,
		  long arg0, long arg1, long arg2, long arg3, long arg4,
		  long arg5, long arg6, long arg7, long arg8, long arg9)
{
	WIND_TCB tcb;

	if (taskInit(&tcb, name, prio, flags, NULL, stacksize, entry,
		     arg0, arg1, arg2, arg3, arg4,
		     arg5, arg6, arg7, arg8, arg9) == ERROR)
		return ERROR;

	return taskActivate(tcb.handle) == ERROR ? ERROR : tcb.handle;
}

STATUS taskDelete(TASK_ID task_id)
{
	TASK_DESC desc;
	pthread_t tid;
	int err;

	err = XENOMAI_SKINCALL2(__vxworks_muxid,
				__vxworks_taskinfo_get, task_id, &desc);
	if (err) {
		errno = abs(err);
		return ERROR;
	}

	tid = (pthread_t)desc.td_opaque;
	if (tid == pthread_self()) {
		/* Silently migrate to avoid raising SIGXCPU. */
		XENOMAI_SYSCALL1(__xn_sys_migrate, XENOMAI_LINUX_DOMAIN);
		pthread_exit(NULL);
	}

	/*
	 * Serialize and lock out anyone from safe sections. We won't
	 * release this lock, which is untracked (no PIP) and lives
	 * within the target thread TCB, so that is ok.
	 */
	XENOMAI_SKINCALL1(__vxworks_muxid, __vxworks_task_safe, task_id);

	if (tid) {
		err = pthread_cancel(tid);
		if (err)
			return -err;
	}

	err = XENOMAI_SKINCALL1(__vxworks_muxid, __vxworks_task_delete, task_id);
	if (err == S_objLib_OBJ_ID_ERROR)
		return OK; /* Used to be valid, but has exited. */

	if (err) {
		errno = abs(err);
		return ERROR;
	}

	return OK;
}

STATUS taskDeleteForce(TASK_ID task_id)
{
	TASK_DESC desc;
	pthread_t tid;
	int err;

	err = XENOMAI_SKINCALL2(__vxworks_muxid,
				__vxworks_taskinfo_get, task_id, &desc);
	if (err) {
		errno = abs(err);
		return ERROR;
	}

	tid = (pthread_t)desc.td_opaque;
	if (tid == pthread_self()) {
		/* Silently migrate to avoid raising SIGXCPU. */
		XENOMAI_SYSCALL1(__xn_sys_migrate, XENOMAI_LINUX_DOMAIN);
		pthread_exit(NULL);
	}

	if (tid) {
		err = pthread_cancel(tid);
		if (err)
			return -err;
	}

	err = XENOMAI_SKINCALL1(__vxworks_muxid,
				__vxworks_task_deleteforce, task_id);
	if (err == S_objLib_OBJ_ID_ERROR)
		return OK; /* Used to be valid, but has exited. */

	if (err) {
		errno = abs(err);
		return ERROR;
	}

	return OK;
}

STATUS taskSuspend(TASK_ID task_id)
{
	int err;

	err =
	    XENOMAI_SKINCALL1(__vxworks_muxid, __vxworks_task_suspend, task_id);
	if (err) {
		errno = abs(err);
		return ERROR;
	}

	return OK;
}

STATUS taskResume(TASK_ID task_id)
{
	int err;

	err =
	    XENOMAI_SKINCALL1(__vxworks_muxid, __vxworks_task_resume, task_id);
	if (err) {
		errno = abs(err);
		return ERROR;
	}

	return OK;
}

TASK_ID taskIdSelf(void)
{
#ifdef HAVE___THREAD
	return __vxworks_self.handle;

#else /* !HAVE___THREAD */
	WIND_TCB *self;
	int err;

	self = (WIND_TCB *)pthread_getspecific(__vxworks_tskey);

	if (self)
		return self->handle;

	self = (WIND_TCB *)malloc(sizeof(*self));

	if (!self) {
		errno = S_memLib_NOT_ENOUGH_MEMORY;
		return ERROR;
	}

	err = XENOMAI_SKINCALL1(__vxworks_muxid, __vxworks_task_self, self);

	if (err) {
		errno = abs(err);
		return ERROR;
	}

	pthread_setspecific(__vxworks_tskey, self);

	return self->handle;
#endif /* !HAVE___THREAD */
}

STATUS taskPrioritySet(TASK_ID task_id, int prio)
{
	int err;

	err = XENOMAI_SKINCALL2(__vxworks_muxid,
				__vxworks_task_priorityset, task_id, prio);
	if (err) {
		errno = abs(err);
		return ERROR;
	}

	return OK;
}

STATUS taskPriorityGet(TASK_ID task_id, int *pprio)
{
	int err;

	err = XENOMAI_SKINCALL2(__vxworks_muxid,
				__vxworks_task_priorityget, task_id, pprio);
	if (err) {
		errno = abs(err);
		return ERROR;
	}

	return OK;
}

STATUS taskLock(void)
{
	XENOMAI_SKINCALL0(__vxworks_muxid, __vxworks_task_lock);
	return OK;
}

STATUS taskUnlock(void)
{
	XENOMAI_SKINCALL0(__vxworks_muxid, __vxworks_task_unlock);
	return OK;
}

STATUS taskSafe(void)
{
	XENOMAI_SKINCALL1(__vxworks_muxid, __vxworks_task_safe, 0);
	return OK;
}

STATUS taskUnsafe(void)
{
	int err = XENOMAI_SKINCALL0(__vxworks_muxid,
				    __vxworks_task_unsafe);
	if (err) {
		errno = abs(err);
		return ERROR;
	}

	return OK;
}

STATUS taskDelay(int ticks)
{
	int err = XENOMAI_SKINCALL1(__vxworks_muxid,
				    __vxworks_task_delay,
				    ticks);
	if (err) {
		errno = abs(err);
		return ERROR;
	}

	return OK;
}

STATUS taskIdVerify(TASK_ID task_id)
{
	int err = XENOMAI_SKINCALL1(__vxworks_muxid,
				    __vxworks_task_verifyid,
				    task_id);
	if (err) {
		errno = abs(err);
		return ERROR;
	}

	return OK;
}

TASK_ID taskNameToId(const char *name)
{
	WIND_TCB tcb;

	int err = XENOMAI_SKINCALL2(__vxworks_muxid,
				    __vxworks_task_nametoid,
				    name,
				    &tcb);
	if (err) {
		errno = abs(err);
		return ERROR;
	}

	return tcb.handle;
}

void taskExit(int code)
{
	pthread_exit((void *)(long) code);
}

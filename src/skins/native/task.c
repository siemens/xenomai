/*
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
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
#include <pthread.h>
#include <signal.h>
#include <limits.h>
#include <pthread.h>
#include <native/syscall.h>
#include <native/task.h>
#include <asm-generic/bits/sigshadow.h>
#include <asm-generic/bits/current.h>
#include <asm-generic/stack.h>
#include "wrappers.h"

#ifdef HAVE___THREAD
__thread RT_TASK __native_self __attribute__ ((tls_model ("initial-exec"))) = {
	.opaque = XN_NO_HANDLE,
	.opaque2 = 0
};
#else /* !HAVE___THREAD */
extern pthread_key_t __native_tskey;
#endif /* !HAVE___THREAD */

extern int __native_muxid;

/* Public Xenomai interface. */

struct rt_task_iargs {
	RT_TASK *task;
	const char *name;
	int prio;
	int mode;
	xncompletion_t *completionp;
};

static void *rt_task_trampoline(void *cookie)
{
	struct rt_task_iargs *iargs = (struct rt_task_iargs *)cookie;
	volatile pthread_t tid = pthread_self();
	void (*entry) (void *cookie);
	unsigned long mode_offset;
	struct rt_arg_bulk bulk;
	RT_TASK *task, *self;
	long err;

#ifdef HAVE___THREAD
	self = &__native_self;
#else /* !HAVE___THREAD */
	self = malloc(sizeof(*self));
	pthread_setspecific(__native_tskey, self);
#endif /* !HAVE___THREAD */

	/* rt_task_delete requires asynchronous cancellation */
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	xeno_sigshadow_install_once();

	task = iargs->task;
	bulk.a1 = (u_long)task;
	bulk.a2 = (u_long)iargs->name;
	bulk.a3 = (u_long)iargs->prio;
	bulk.a4 = (u_long)iargs->mode;
	bulk.a5 = (u_long)tid;
	/* Signal allocation failures by setting bulk.a6 to 0, they will be
	   propagated to the thread waiting in xn_sys_completion. */
	bulk.a6 = !self ? 0UL : (u_long)&mode_offset;

	asm volatile("nop;nop;nop");
	err = XENOMAI_SKINCALL2(__native_muxid,
				__native_task_create, &bulk,
				iargs->completionp);
	if (!bulk.a6) {
		err = -ENOMEM;
		goto fail;
	}
	if (err)
		goto fail;

	*self = *task;

	xeno_set_current();
	xeno_set_current_mode(mode_offset);

	/* Wait on the barrier for the task to be started. The barrier
	   could be released in order to process Linux signals while the
	   Xenomai shadow is still dormant; in such a case, resume wait. */

	do
		err = XENOMAI_SYSCALL2(__xn_sys_barrier, &entry, &cookie);
	while (err == -EINTR);

	if (!err)
		entry(cookie);

	/* Silently migrate to avoid raising SIGXCPU on regular exit. */
	XENOMAI_SYSCALL1(__xn_sys_migrate, XENOMAI_LINUX_DOMAIN);

      fail:

	return (void *)err;
}

int rt_task_create(RT_TASK *task,
		   const char *name, int stksize, int prio, int mode)
{
	struct rt_task_iargs iargs;
	xncompletion_t completion;
	struct sched_param param;
	pthread_attr_t thattr;
	pthread_t thid;
	int err;

	/* Migrate this thread to the Linux domain since we are about to
	   issue a series of regular kernel syscalls in order to create
	   the new Linux thread, which in turn will be mapped to a
	   real-time shadow. */

	XENOMAI_SYSCALL1(__xn_sys_migrate, XENOMAI_LINUX_DOMAIN);

	completion.syncflag = 0;
	completion.pid = -1;

	iargs.task = task;
	iargs.name = name;
	iargs.prio = prio;
	iargs.mode = mode;
	iargs.completionp = &completion;

	pthread_attr_init(&thattr);

	stksize = xeno_stacksize(stksize);

	pthread_attr_setinheritsched(&thattr, PTHREAD_EXPLICIT_SCHED);
	memset(&param, 0, sizeof(param));
	if (prio > 0) {
		pthread_attr_setschedpolicy(&thattr, SCHED_FIFO);
		param.sched_priority = prio;
	} else
		pthread_attr_setschedpolicy(&thattr, SCHED_OTHER);
	pthread_attr_setschedparam(&thattr, &param);
	pthread_attr_setstacksize(&thattr, stksize);
	if (!(mode & T_JOINABLE))
		pthread_attr_setdetachstate(&thattr, PTHREAD_CREATE_DETACHED);

	err = __real_pthread_create(&thid, &thattr, &rt_task_trampoline, &iargs);

	if (err)
		return -err;

	/* Wait for sync with rt_task_trampoline() */
	err = XENOMAI_SYSCALL1(__xn_sys_completion, &completion);
	if (err && (mode & T_JOINABLE))
		pthread_join(thid, NULL);

	return err;
}

int rt_task_start(RT_TASK *task, void (*entry) (void *cookie), void *cookie)
{
	return XENOMAI_SKINCALL3(__native_muxid,
				 __native_task_start, task, entry, cookie);
}

int rt_task_shadow(RT_TASK *task, const char *name, int prio, int mode)
{
	unsigned long mode_offset;
	struct rt_arg_bulk bulk;
	RT_TASK task_desc;
	RT_TASK *self;
	int err;

	xeno_fault_stack();

#ifdef HAVE___THREAD
	self = &__native_self;
#else /* !HAVE___THREAD */
	if (pthread_getspecific(__native_tskey))
		/* Current task is already a native taks. */
		return -EBUSY;

	self = malloc(sizeof(*self));
	if (!self)
		return -ENOMEM;

	pthread_setspecific(__native_tskey, self);
#endif /* !HAVE___THREAD */

	if (task == NULL)
		task = &task_desc; /* Discarded. */

	/* rt_task_delete requires asynchronous cancellation */
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	xeno_sigshadow_install_once();

	bulk.a1 = (u_long)task;
	bulk.a2 = (u_long)name;
	bulk.a3 = (u_long)prio;
	bulk.a4 = (u_long)mode;
	bulk.a5 = (u_long)pthread_self();
	bulk.a6 = (u_long)&mode_offset;

	err = XENOMAI_SKINCALL2(__native_muxid, __native_task_create, &bulk,
				NULL);
	if (err)
		goto fail;

	*self = *task;

	xeno_set_current();
	xeno_set_current_mode(mode_offset);

	return 0;

  fail:
#ifndef HAVE___THREAD
	pthread_setspecific(__native_tskey, NULL);
	free(self);
#endif /* !HAVE___THREAD */
	return err;
}

int rt_task_bind(RT_TASK *task, const char *name, RTIME timeout)
{
	return XENOMAI_SKINCALL3(__native_muxid,
				 __native_task_bind, task, name, &timeout);
}

int rt_task_suspend(RT_TASK *task)
{
	return XENOMAI_SKINCALL1(__native_muxid, __native_task_suspend, task);
}

int rt_task_resume(RT_TASK *task)
{
	return XENOMAI_SKINCALL1(__native_muxid, __native_task_resume, task);
}

int rt_task_delete(RT_TASK *task)
{
	RT_TASK *self;
	int err;

	if (task == NULL ||
	    ((self = rt_task_self()) && self->opaque == task->opaque)) {
		/* Silently migrate to avoid raising SIGXCPU. */
		XENOMAI_SYSCALL1(__xn_sys_migrate, XENOMAI_LINUX_DOMAIN);
		pthread_exit(NULL);
	}

	if (task->opaque2) {
		err = pthread_cancel((pthread_t)task->opaque2);
		if (err)
			return -err;
	}

	err = XENOMAI_SKINCALL1(__native_muxid, __native_task_delete, task);
	if (err == -ESRCH)
		return 0;

	return err;
}

int rt_task_yield(void)
{
	return XENOMAI_SKINCALL0(__native_muxid, __native_task_yield);
}

int rt_task_set_periodic(RT_TASK *task, RTIME idate, RTIME period)
{
	return XENOMAI_SKINCALL3(__native_muxid,
				 __native_task_set_periodic, task, &idate,
				 &period);
}

int rt_task_wait_period(unsigned long *overruns_r)
{
	int err, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	err = XENOMAI_SKINCALL1(__native_muxid,
				 __native_task_wait_period, overruns_r);

	pthread_setcanceltype(oldtype, NULL);

	return err;
}

int rt_task_set_priority(RT_TASK *task, int prio)
{
	return XENOMAI_SKINCALL2(__native_muxid,
				 __native_task_set_priority, task, prio);
}

int rt_task_sleep(RTIME delay)
{
	int err, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	err = XENOMAI_SKINCALL1(__native_muxid, __native_task_sleep, &delay);

	pthread_setcanceltype(oldtype, NULL);

	return err;
}

int rt_task_sleep_until(RTIME date)
{
	int err, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	err = XENOMAI_SKINCALL1(__native_muxid, __native_task_sleep_until,
				 &date);

	pthread_setcanceltype(oldtype, NULL);

	return err;
}

int rt_task_unblock(RT_TASK *task)
{
	return XENOMAI_SKINCALL1(__native_muxid, __native_task_unblock, task);
}

int rt_task_inquire(RT_TASK *task, RT_TASK_INFO *info)
{
	return XENOMAI_SKINCALL2(__native_muxid, __native_task_inquire, task,
				 info);
}

int rt_task_notify(RT_TASK *task, rt_sigset_t signals)
{
	return XENOMAI_SKINCALL2(__native_muxid,
				 __native_task_notify, task, signals);
}

int rt_task_set_mode(int clrmask, int setmask, int *oldmode)
{
	int err;

	err = XENOMAI_SKINCALL3(__native_muxid,
				__native_task_set_mode, clrmask, setmask,
				oldmode);

	return err;
}

RT_TASK *rt_task_self(void)
{
	RT_TASK *self;

#ifdef HAVE___THREAD
	self = &__native_self;

	if (self->opaque == XN_NO_HANDLE)
		return NULL;
#else /* !HAVE___THREAD */
	self = pthread_getspecific(__native_tskey);
#endif /* !HAVE___THREAD */

	return self;
}

int rt_task_slice(RT_TASK *task, RTIME quantum)
{
	return XENOMAI_SKINCALL2(__native_muxid,
				 __native_task_slice, task, &quantum);
}

int rt_task_join(RT_TASK *task)
{
	if (!task->opaque2)
		return -ESRCH;

	return -pthread_join((pthread_t)task->opaque2, NULL);
}

ssize_t rt_task_send(RT_TASK *task,
		     RT_TASK_MCB *mcb_s, RT_TASK_MCB *mcb_r, RTIME timeout)
{
	int oldtype;
	ssize_t ret;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	ret = (ssize_t)XENOMAI_SKINCALL4(__native_muxid,
					 __native_task_send,
					 task, mcb_s, mcb_r, &timeout);

	pthread_setcanceltype(oldtype, NULL);

	return ret;
}

int rt_task_receive(RT_TASK_MCB *mcb_r, RTIME timeout)
{
	return XENOMAI_SKINCALL2(__native_muxid,
				 __native_task_receive, mcb_r, &timeout);
}

int rt_task_reply(int flowid, RT_TASK_MCB *mcb_s)
{
	return XENOMAI_SKINCALL2(__native_muxid,
				 __native_task_reply, flowid, mcb_s);
}

int rt_task_same(RT_TASK *task1, RT_TASK *task2)
{
	return task1->opaque == task2->opaque;
}

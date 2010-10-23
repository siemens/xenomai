/*
 * Copyright (C) 2008 Philippe Gerum <rpm@xenomai.org>.
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

#include <stdio.h>
#include <memory.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include <fcntl.h>
#include <sched.h>
#include <limits.h>
#include "taskLib.h"
#include "tickLib.h"
#include <copperplate/panic.h>
#include <copperplate/heapobj.h>
#include <copperplate/threadobj.h>
#include <copperplate/syncobj.h>
#include <vxworks/errnoLib.h>

struct pvhash_table wind_task_table;

static unsigned long anon_tids;

static struct wind_task *find_wind_task(TASK_ID tid)
{
	struct WIND_TCB *tcb = mainheap_deref(tid, struct WIND_TCB);

	/*
	 * Best-effort to validate a TCB pointer the cheap way,
	 * without relying on any syscall.
	 */
	if (tcb == NULL || ((intptr_t)tcb & (sizeof(intptr_t)-1)) != 0 ||
	    tcb->magic != task_magic)
		return NULL;

	return tcb->opaque;
}

static struct wind_task *find_wind_task_or_self(TASK_ID tid)
{
	if (tid)
		return find_wind_task(tid);

	return wind_task_current();
}

struct wind_task *get_wind_task(TASK_ID tid)
{
	struct wind_task *task = find_wind_task(tid);

	/*
	 * Grab the task lock, assuming that the task might have been
	 * deleted, and/or maybe we have been lucky, and some random
	 * opaque pointer might lead us to something which is laid in
	 * valid memory but certainly not to a task object. Last
	 * chance is pthread_mutex_lock() detecting a wrong mutex kind
	 * and bailing out.
	 */
	if (task == NULL || threadobj_lock(&task->thobj) == -EINVAL)
		return NULL;

	/* Check the magic word again, while we hold the lock. */
	if (task->tcb->magic != task_magic) {
		threadobj_unlock(&task->thobj);
		return NULL;
	}

	return task;
}

struct wind_task *get_wind_task_or_self(TASK_ID tid)
{
	struct wind_task *current;

	if (tid)
		return get_wind_task(tid);

	current = wind_task_current();
	if (current == NULL)
		return NULL;

	/* This one might block but can't fail, it is ours. */
	threadobj_lock(&current->thobj);

	/* Check the magic word again, while we hold the lock. */
	if (threadobj_get_magic(&current->thobj) != task_magic) {
		threadobj_unlock(&current->thobj);
		return NULL;
	}

	return current;
}

void put_wind_task(struct wind_task *task)
{
	threadobj_unlock(&task->thobj);
}

static void task_finalizer(struct threadobj *thobj)
{
	struct wind_task *task = container_of(thobj, struct wind_task, thobj);

	threadobj_lock(&task->thobj);
	threadobj_set_magic(&task->thobj, ~task_magic);
	task->tcb->magic = ~task_magic; /* In case of normal exit. */
	task->tcb->status |= WIND_DEAD;
	pvhash_remove(&wind_task_table, &task->obj);
	threadobj_unlock(&task->thobj);

	registry_remove_file(&task->fsobj);
	sem_destroy(&task->barrier);
	pthread_mutex_destroy(&task->safelock);
	threadobj_destroy(&task->thobj);

	xnfree(task);
}

/*
 * XXX: Wait and suspend hooks run on behalf of the target task, so no
 * lock is needed to prevent unexpected exit while accessing their
 * TCB.
 */
static void task_wait_hook(struct threadobj *thobj, int status)
{
	struct wind_task *task = container_of(thobj, struct wind_task, thobj);

	if (status & SYNCOBJ_BLOCK)
		task->tcb->status |= WIND_PEND;
	else
		task->tcb->status &= ~WIND_PEND;
}

static void task_suspend_hook(struct threadobj *thobj, int status)
{
	struct wind_task *task = container_of(thobj, struct wind_task, thobj);

	if (status & THREADOBJ_SUSPEND)
		task->tcb->status |= WIND_SUSPEND;
	else
		task->tcb->status &= ~WIND_SUSPEND;
}

#ifdef CONFIG_XENO_REGISTRY

static inline char *task_decode_status(struct wind_task *task, char *buf)
{
	int status;

	*buf = '\0';
	status = threadobj_get_status(&task->thobj);
	if (status & THREADOBJ_SCHEDLOCK)
		strcat(buf, "+sched_lock");
	if (status & THREADOBJ_ROUNDROBIN)
		strcat(buf, "+sched_rr");

	status = task->tcb->status;
	if (status == WIND_READY)
		strcat(buf, "+ready");
	else {
		if (status & WIND_SUSPEND)
			strcat(buf, "+suspended");
		if (status & WIND_PEND)
			strcat(buf, "+pending");
		if (status & WIND_DELAY)
			strcat(buf, "+delayed");
	}

	return buf + 1;
}

static size_t task_registry_read(struct fsobj *fsobj,
				 char *buf, size_t size, off_t offset)
{
	struct wind_task *task;
	char sbuf[64];
	size_t len;

	task = container_of(fsobj, struct wind_task, fsobj);
	len =  sprintf(buf,       "name       = %s\n", task->name);
	len += sprintf(buf + len, "errno      = %d\n", threadobj_get_errno(&task->thobj));
	len += sprintf(buf + len, "status     = %s\n", task_decode_status(task, sbuf));
	len += sprintf(buf + len, "priority   = %d\n", wind_task_get_priority(task));
	len += sprintf(buf + len, "lock_depth = %d\n", threadobj_get_lockdepth(&task->thobj));

	return len;
}

static struct registry_operations registry_ops = {
	.read	= task_registry_read
};

#else

static struct registry_operations registry_ops;

#endif /* CONFIG_XENO_REGISTRY */

static void *task_trampoline(void *arg)
{
	struct wind_task *task = arg;
	struct wind_task_args *args = &task->args;
	int ret;

	ret = threadobj_prologue(&task->thobj);
	if (ret) {
		warning("task %s prologue failed (errno=%d)",
			task->name, -ret);
		goto done;
	}
	
	ret = registry_init_file(&task->fsobj, &registry_ops);

	if (pvhash_enter(&wind_task_table, task->name, &task->obj)) {
		warning("duplicate task name: %s", task->name);
		/* Make sure we won't un-hash the previous one. */
		strcpy(task->name, "(dup)");
	} else if (ret == 0)
		ret = registry_add_file(&task->fsobj, O_RDONLY,
					"/vxworks/tasks/%s", task->name);
	if (ret)
		warning("failed to export task %s to registry",
			task->name);

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	/* Wait for someone to run taskActivate() upon us. */
	sem_wait(&task->barrier);

	args->entry(args->arg0, args->arg1, args->arg2, args->arg3,
		    args->arg4, args->arg5, args->arg6, args->arg7,
		    args->arg8, args->arg9);
done:
	pthread_exit((void *)(long)ret);
}

static int check_task_priority(int wind_prio)
{
	if (wind_prio < 0 || wind_prio > 255) /* In theory. */
		return S_taskLib_ILLEGAL_PRIORITY;

	if (wind_prio >= threadobj_max_prio - 1) /* In practice. */
		panic("current implementation restricts VxWorks "
		      "priority levels to range [%d..0]",
		      threadobj_max_prio - 2);
	return OK;
}

static STATUS __taskInit(struct wind_task *task,
			 struct WIND_TCB *tcb, const char *name,
			 int prio, int flags, FUNCPTR entry, int stacksize)
{
	struct threadobj_init_data idata;
	pthread_mutexattr_t mattr;
	struct sched_param param;
	pthread_attr_t thattr;
	int ret;

	ret = check_task_priority(prio);
	if (ret) {
		errno = ret;
		return ERROR;
	}

	if (name == NULL || *name == '\0')
		sprintf(task->name, "t%lu", ++anon_tids);
	else {
		strncpy(task->name, name, sizeof(task->name));
		task->name[sizeof(task->name) - 1] = '\0';
	}

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, mutex_scope_attribute);
	pthread_mutex_init(&task->safelock, &mattr);
	pthread_mutexattr_destroy(&mattr);
	sem_init(&task->barrier, sem_scope_attribute, 0);

	task->tcb = tcb;
	tcb->magic = task_magic;
	tcb->opaque = task;
	tcb->status = WIND_SUSPEND;
	tcb->safeCnt = 0;
	tcb->flags = flags;
	tcb->entry = entry;

	pthread_attr_init(&thattr);

	if (stacksize == 0)
		stacksize = PTHREAD_STACK_MIN * 4;
	else if (stacksize < PTHREAD_STACK_MIN)
		stacksize = PTHREAD_STACK_MIN;

	memset(&param, 0, sizeof(param));
	param.sched_priority = wind_task_normalize_priority(prio);
	pthread_attr_setinheritsched(&thattr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedparam(&thattr, &param);
	pthread_attr_setschedpolicy(&thattr, SCHED_FIFO);
	pthread_attr_setstacksize(&thattr, stacksize);
	pthread_attr_setscope(&thattr, thread_scope_attribute);

	idata.magic = task_magic;
	idata.wait_hook = task_wait_hook;
	idata.suspend_hook = task_suspend_hook;
	idata.finalizer = task_finalizer;
	threadobj_init(&task->thobj, &idata);

	ret = pthread_create(&task->thobj.tid, &thattr, &task_trampoline, task);
	pthread_attr_destroy(&thattr);
	if (ret) {
		errno = ret == -EAGAIN ? S_memLib_NOT_ENOUGH_MEMORY : -ret;
		return ERROR;
	}

	return OK;
}

static inline struct wind_task *alloc_task(void)
{
	return xnmalloc(sizeof(struct wind_task));
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
	struct wind_task_args *args;
	struct wind_task *task;

	if (threadobj_async_p()) {
		errno = S_intLib_NOT_ISR_CALLABLE;
		return ERROR;
	}
	  
	task = alloc_task();
	if (task == NULL) {
		errno = S_memLib_NOT_ENOUGH_MEMORY;
		return ERROR;
	}

	args = &task->args;
	args->entry = entry;
	args->arg0 = arg0;
	args->arg1 = arg1;
	args->arg2 = arg2;
	args->arg3 = arg3;
	args->arg4 = arg4;
	args->arg5 = arg5;
	args->arg6 = arg6;
	args->arg7 = arg7;
	args->arg8 = arg8;
	args->arg9 = arg9;

	return __taskInit(task, pTcb, name, prio, flags, entry, stacksize);
}

STATUS taskActivate(TASK_ID tid)
{
	struct wind_task *task = get_wind_task(tid);

	if (task == NULL) {
		errno = S_objLib_OBJ_ID_ERROR;
		return ERROR;
	}

	task->tcb->status &= ~WIND_SUSPEND;
	put_wind_task(task);
	/* Assume sem_post() will check for validity. */
	sem_post(&task->barrier);

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
	struct wind_task_args *args;
	struct wind_task *task;
	TASK_ID tid;

	if (threadobj_async_p()) {
		errno = S_intLib_NOT_ISR_CALLABLE;
		return ERROR;
	}
	  
	task = alloc_task();
	if (task == NULL) {
		errno = S_memLib_NOT_ENOUGH_MEMORY;
		return ERROR;
	}

	args = &task->args;
	args->entry = entry;
	args->arg0 = arg0;
	args->arg1 = arg1;
	args->arg2 = arg2;
	args->arg3 = arg3;
	args->arg4 = arg4;
	args->arg5 = arg5;
	args->arg6 = arg6;
	args->arg7 = arg7;
	args->arg8 = arg8;
	args->arg9 = arg9;

	if (__taskInit(task, &task->priv_tcb, name,
		       prio, flags, entry, stacksize) == ERROR)
		return ERROR;

	tid = mainheap_ref(&task->priv_tcb, TASK_ID);

	return taskActivate(tid) == ERROR ? ERROR : tid;
}

static STATUS __taskDelete(TASK_ID tid, int force)
{
	struct wind_task *task = find_wind_task_or_self(tid);
	int ret;

	if (threadobj_async_p()) {
		errno = S_intLib_NOT_ISR_CALLABLE;
		return ERROR;
	}
	  
	if (task == NULL)
		goto objid_error;

	if (task == wind_task_current()) /* Self-deletion. */
		pthread_exit(NULL);

	/*
	 * We always attempt to grab the thread safe lock first, then
	 * make sure nobody (including the target task itself) will be
	 * able to alter the internal state of that task anymore. In
	 * forced mode, we are allowed to bypass lock contention, but
	 * then we might create dangerous situations leading to
	 * invalid memory references; that's just part of the deal.
	 * NOTE: Locking order is always safelock first, internal
	 * object lock afterwards, therefore, _never_ call
	 * __taskDelete() directly or indirectly while holding the
	 * thread object lock. You have been warned.
	 */
	if (force)	/* Best effort only. */
		force = pthread_mutex_trylock(&task->safelock);
	else
		pthread_mutex_lock(&task->safelock);

	threadobj_lock(&task->thobj);
	/*
	 * This basically makes the thread enter a zombie state, since
	 * it won't be reachable by anyone after its magic has been
	 * trashed.
	 */
	task->tcb->magic = ~task_magic;
	threadobj_set_magic(&task->thobj, ~task_magic);
	threadobj_unlock(&task->thobj);

	if (!force)	/* I.e. do we own the safe lock? */
		pthread_mutex_unlock(&task->safelock);

	ret = threadobj_cancel(&task->thobj);
	if (ret) {
	objid_error:
		errno = S_objLib_OBJ_ID_ERROR;
		return ERROR;
	}

	return OK;
}

STATUS taskDelete(TASK_ID tid)
{
	return __taskDelete(tid, 0);
}

STATUS taskDeleteForce(TASK_ID tid)
{
	return __taskDelete(tid, 1);
}

TASK_ID taskIdSelf(void)
{
	struct wind_task *current;

	if (threadobj_async_p()) {
		errno = S_intLib_NOT_ISR_CALLABLE;
		return ERROR;
	}
	  
	current = wind_task_current();

	if (current == NULL) {
		errno = S_objLib_OBJ_NO_METHOD;
		return ERROR;
	}
	  
	return (TASK_ID)current->tcb;
}

struct WIND_TCB *taskTcb(TASK_ID tid)
{
	struct wind_task *task = find_wind_task(tid);

	if (task == NULL) {
		errno = S_objLib_OBJ_ID_ERROR;
		return NULL;
	}

	return task->tcb;
}

STATUS taskSuspend(TASK_ID tid)
{
	struct wind_task *task = get_wind_task(tid);
	int ret;

	if (task == NULL)
		goto objid_error;

	ret = threadobj_suspend(&task->thobj);
	put_wind_task(task);

	if (ret) {
	objid_error:
		errno = S_objLib_OBJ_ID_ERROR;
		return ERROR;
	}

	return OK;
}

STATUS taskResume(TASK_ID tid)
{
	struct wind_task *task = get_wind_task(tid);
	int ret;

	if (task == NULL)
		goto objid_error;

	ret = threadobj_resume(&task->thobj);
	put_wind_task(task);

	if (ret) {
	objid_error:
		errno = S_objLib_OBJ_ID_ERROR;
		return ERROR;
	}

	return OK;
}

STATUS taskSafe(void)
{
	struct wind_task *current;

	if (threadobj_async_p()) {
		errno = S_intLib_NOT_ISR_CALLABLE;
		return ERROR;
	}

	current = wind_task_current();
	if (current == NULL) {
		errno = S_objLib_OBJ_NO_METHOD;
		return ERROR;
	}

	pthread_mutex_lock(&current->safelock);
	current->tcb->safeCnt++;

	return OK;
}

STATUS taskUnsafe(void)
{
	struct wind_task *current;
	int ret;

	if (threadobj_async_p()) {
		errno = S_intLib_NOT_ISR_CALLABLE;
		return ERROR;
	}

	current = wind_task_current();
	if (current == NULL) {
		errno = S_objLib_OBJ_NO_METHOD;
		return ERROR;
	}

	ret = pthread_mutex_unlock(&current->safelock);
	if (ret == 0)
		current->tcb->safeCnt--;

	return OK;
}

STATUS taskIdVerify(TASK_ID tid)
{
	struct wind_task *task = find_wind_task(tid);

	if (task == NULL) {
		errno = S_objLib_OBJ_ID_ERROR;
		return ERROR;
	}

	return OK;
}

void taskExit(int code)
{
	pthread_exit((void *)(long)code);
}

STATUS taskPrioritySet(TASK_ID tid, int prio)
{
	struct wind_task *task = get_wind_task(tid);
	int ret, pprio;

	if (task == NULL)
		goto objid_error;

	ret = check_task_priority(prio);

	if (ret) {
		put_wind_task(task);
		errno = ret;
		return ERROR;
	}

	pprio = wind_task_normalize_priority(prio);
	ret = threadobj_set_priority(&task->thobj, pprio);
	put_wind_task(task);

	if (ret) {
	objid_error:
		errno = S_objLib_OBJ_ID_ERROR;
		return ERROR;
	}

	return OK;
}

int wind_task_get_priority(struct wind_task *task)
{
	/* Can't fail if we hold the task lock as we should. */
	int prio = threadobj_get_priority(&task->thobj);
	return wind_task_denormalize_priority(prio);
}

STATUS taskPriorityGet(TASK_ID tid, int *priop)
{
	struct wind_task *task = get_wind_task(tid);

	if (task == NULL) {
		errno = S_objLib_OBJ_ID_ERROR;
		return ERROR;
	}

	*priop = wind_task_get_priority(task);

	put_wind_task(task);

	return OK;
}

STATUS taskLock(void)
{
	struct wind_task *task;

	if (threadobj_async_p()) {
		errno = S_intLib_NOT_ISR_CALLABLE;
		return ERROR;
	}

	task = get_wind_task_or_self(0);
	if (task == NULL) {
		errno = S_objLib_OBJ_ID_ERROR;
		return ERROR;
	}

	threadobj_lock_sched(&task->thobj);
	put_wind_task(task);

	return OK;
}

STATUS taskUnlock(void)
{
	struct wind_task *task;

	if (threadobj_async_p()) {
		errno = S_intLib_NOT_ISR_CALLABLE;
		return ERROR;
	}
	  
	task = get_wind_task_or_self(0);
	if (task == NULL) {
		errno = S_objLib_OBJ_ID_ERROR;
		return ERROR;
	}

	threadobj_unlock_sched(&task->thobj);
	put_wind_task(task);

	return OK;
}

STATUS taskDelay(int ticks)
{
	struct wind_task *current;
	struct timespec rqt;
	int ret;

	if (threadobj_async_p()) {
		errno = S_intLib_NOT_ISR_CALLABLE;
		return ERROR;
	}

	current = wind_task_current();

	if (current == NULL) {
		errno = S_objLib_OBJ_NO_METHOD;
		return ERROR;
	}

	if (ticks == 0) {
		sched_yield();	/* Manual round-robin. */
		return OK;
	}

	clockobj_ticks_to_timeout(&wind_clock, ticks, &rqt);
	current->tcb->status |= WIND_DELAY;
	ret = threadobj_sleep(&rqt, NULL);
	current->tcb->status &= ~WIND_DELAY;

	if (ret) {
		errno = -ret;
		return ERROR;
	}

	return OK;
}

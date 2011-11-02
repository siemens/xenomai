/*
 * Copyright (C) 2011 Philippe Gerum <rpm@xenomai.org>.
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

#include <assert.h>
#include <sched.h>
#include <pthread.h>
#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <copperplate/heapobj.h>
#include "internal.h"
#include "task.h"
#include "timer.h"

struct syncluster alchemy_task_table;

static struct alchemy_namegen task_namegen = {
	.prefix = "task",
	.length = sizeof ((struct alchemy_task *)0)->name,
};

static void delete_tcb(struct alchemy_task *tcb);

static struct alchemy_task *find_alchemy_task(RT_TASK *task, int *err_r)
{
	struct alchemy_task *tcb;
	unsigned int magic;

	if (task == NULL || ((intptr_t)task & (sizeof(intptr_t)-1)) != 0)
		goto bad_handle;

	tcb = mainheap_deref(task->handle, struct alchemy_task);
	if (tcb == NULL || ((intptr_t)tcb & (sizeof(intptr_t)-1)) != 0)
		goto bad_handle;

	magic = threadobj_get_magic(&tcb->thobj);
	if (magic == task_magic)
		return tcb;

	if (magic == ~task_magic) {
		*err_r = -EIDRM;
		return NULL;
	}

bad_handle:
	*err_r = -EINVAL;

	return NULL;
}

static struct alchemy_task *find_alchemy_task_or_self(RT_TASK *task, int *err_r)
{
	struct alchemy_task *current;

	if (task)
		return find_alchemy_task(task, err_r);

	current = alchemy_task_current();
	if (current == NULL) {
		*err_r = -EPERM;
		return NULL;
	}
		
	return current;
}

struct alchemy_task *get_alchemy_task(RT_TASK *task, int *err_r)
{
	struct alchemy_task *tcb = find_alchemy_task(task, err_r);

	/*
	 * Grab the task lock, assuming that the task might have been
	 * deleted, and/or maybe we have been lucky, and some random
	 * opaque pointer might lead us to something which is laid in
	 * valid memory but certainly not to a task object. Last
	 * chance is pthread_mutex_lock() detecting a wrong mutex kind
	 * and bailing out.
	 */
	if (tcb == NULL || threadobj_lock(&tcb->thobj) == -EINVAL)
		return NULL;

	/* Check the magic word again, while we hold the lock. */
	if (threadobj_get_magic(&tcb->thobj) != task_magic) {
		threadobj_unlock(&tcb->thobj);
		*err_r = -EIDRM;
		return NULL;
	}

	return tcb;
}

struct alchemy_task *get_alchemy_task_or_self(RT_TASK *task, int *err_r)
{
	struct alchemy_task *current;

	if (task)
		return get_alchemy_task(task, err_r);

	current = alchemy_task_current();
	if (current == NULL) {
		*err_r = -EPERM;
		return NULL;
	}

	/* This one might block but can't fail, it is ours. */
	threadobj_lock(&current->thobj);

	return current;
}

void put_alchemy_task(struct alchemy_task *tcb)
{
	threadobj_unlock(&tcb->thobj);
}

static void task_finalizer(struct threadobj *thobj)
{
	struct alchemy_task *tcb;
	struct syncstate syns;

	tcb = container_of(thobj, struct alchemy_task, thobj);
	syncluster_delobj(&alchemy_task_table, &tcb->cobj);
	syncobj_lock(&tcb->sobj, &syns);
	syncobj_destroy(&tcb->sobj, &syns);
	threadobj_destroy(&tcb->thobj);

	xnfree(tcb);
}

static int task_prologue(struct alchemy_task *tcb)
{
	struct service svc;
	int ret;

	if (CPU_COUNT(&tcb->affinity) > 0) {
		ret = sched_setaffinity(0, sizeof(tcb->affinity),
					&tcb->affinity);
		if (ret)
			warning("cannot set CPU affinity for task %s",
				tcb->name);
	}

	ret = __bt(threadobj_prologue(&tcb->thobj, tcb->name));
	if (ret)
		return ret;

	COPPERPLATE_PROTECT(svc);

	threadobj_wait_start(&tcb->thobj);

	threadobj_lock(&tcb->thobj);

	if (tcb->mode & T_LOCK)
		threadobj_lock_sched(&tcb->thobj);

	threadobj_unlock(&tcb->thobj);

	COPPERPLATE_UNPROTECT(svc);

	return 0;
}

static void *task_trampoline(void *arg)
{
	struct alchemy_task *tcb = arg;
	int ret;

	ret = task_prologue(tcb);
	if (ret) {
		delete_tcb(tcb);
		goto out;
	}

	tcb->entry(tcb->arg);
out:
	threadobj_lock(&tcb->thobj);
	threadobj_set_magic(&tcb->thobj, ~task_magic);
	threadobj_unlock(&tcb->thobj);

	pthread_exit((void *)(long)ret);
}

static int create_tcb(struct alchemy_task **tcbp,
		      const char *name, int prio, int mode)
{
	struct threadobj_init_data idata;
	struct alchemy_task *tcb;
	int cpu, ret;

	ret = check_task_priority(prio);
	if (ret)
		return ret;

	if (mode & ~(T_CPUMASK|T_LOCK))
		return -EINVAL;

	tcb = xnmalloc(sizeof(*tcb));
	if (tcb == NULL)
		return -ENOMEM;

	__alchemy_build_name(tcb->name, name, &task_namegen);

	tcb->mode = mode;
	tcb->entry = NULL;	/* Not yet known. */
	tcb->arg = NULL;

	CPU_ZERO(&tcb->affinity);
	for (cpu = 0; cpu < 8; cpu++) {
		if (T_CPU(cpu))
			CPU_SET(cpu, &tcb->affinity);
	}

	tcb->safecount = 0;
	syncobj_init(&tcb->sobj, 0, fnref_null);

	idata.magic = task_magic;
	idata.wait_hook = NULL;
	idata.suspend_hook = NULL;
	idata.finalizer = task_finalizer;
	idata.priority = prio;
	threadobj_init(&tcb->thobj, &idata);

	if (syncluster_addobj(&alchemy_task_table, tcb->name, &tcb->cobj)) {
		delete_tcb(tcb);
		return -EEXIST;
	}

	*tcbp = tcb;

	return 0;
}

static void delete_tcb(struct alchemy_task *tcb)
{
	struct syncstate syns;

	threadobj_destroy(&tcb->thobj);
	syncobj_lock(&tcb->sobj, &syns);
	syncobj_destroy(&tcb->sobj, &syns);
	xnfree(tcb);
}

int rt_task_create(RT_TASK *task, const char *name,
		   int stksize, int prio, int mode)
{
	struct alchemy_task *tcb;
	struct sched_param param;
	pthread_attr_t thattr;
	struct service svc;
	int policy, ret;

	COPPERPLATE_PROTECT(svc);

	ret = create_tcb(&tcb, name, prio, mode);
	if (ret)
		goto out;

	/* We want this to be set prior to spawning the thread. */
	task->handle = mainheap_ref(tcb, uintptr_t);
	tcb->self = *task;

	pthread_attr_init(&thattr);

	if (stksize < PTHREAD_STACK_MIN * 4)
		stksize = PTHREAD_STACK_MIN * 4;

	memset(&param, 0, sizeof(param));
	param.sched_priority = prio;
	policy = prio ? SCHED_RT : SCHED_OTHER;
	pthread_attr_setinheritsched(&thattr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&thattr, policy);
	pthread_attr_setschedparam(&thattr, &param);
	pthread_attr_setstacksize(&thattr, stksize);
	pthread_attr_setscope(&thattr, thread_scope_attribute);
	pthread_attr_setdetachstate(&thattr, PTHREAD_CREATE_JOINABLE);

	ret = __bt(-__RT(pthread_create(&tcb->thobj.tid, &thattr,
					task_trampoline, tcb)));
	pthread_attr_destroy(&thattr);
	if (ret)
		delete_tcb(tcb);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_task_delete(RT_TASK *task)
{
	struct alchemy_task *tcb;
	struct syncstate syns;
	struct service svc;
	int ret;

 	if (threadobj_async_p())
		return -EPERM;

	tcb = find_alchemy_task_or_self(task, &ret);
	if (tcb == NULL)
		return ret;

	if (tcb == alchemy_task_current()) /* Self-deletion. */
		pthread_exit(NULL);

	COPPERPLATE_PROTECT(svc);

	if (syncobj_lock(&tcb->sobj, &syns)) {
		ret = -EIDRM;
		goto out;
	}

	while (tcb->safecount) {
		ret = syncobj_pend(&tcb->sobj, NULL, &syns);
		if (ret) {
			if (ret == -EIDRM)
				goto out;

			syncobj_unlock(&tcb->sobj, &syns);
			return ret;
		}
	}

	threadobj_lock(&tcb->thobj);
	/*
	 * Prevent further reference to this zombie, including via
	 * alchemy_task_current().
	 */
	threadobj_set_magic(&tcb->thobj, ~task_magic);
	threadobj_unlock(&tcb->thobj);

	syncobj_unlock(&tcb->sobj, &syns);

	ret = threadobj_cancel(&tcb->thobj);
	if (ret)
		ret = -EIDRM;
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_task_start(RT_TASK *task,
		  void (*entry)(void *arg),
		  void *arg)
{
	struct alchemy_task *tcb;
	int ret = 0;

	tcb = get_alchemy_task(task, &ret);
	if (tcb == NULL)
		return ret;

	tcb->entry = entry;
	tcb->arg = arg;
	threadobj_start(&tcb->thobj);
	put_alchemy_task(tcb);

	return 0;
}

int rt_task_shadow(RT_TASK *task, const char *name, int prio, int mode)
{
	struct alchemy_task *tcb;
	struct sched_param param;
	struct service svc;
	int policy, ret;

	COPPERPLATE_PROTECT(svc);

	ret = create_tcb(&tcb, name, prio, mode);
	if (ret)
		goto out;

	tcb->self.handle = mainheap_ref(tcb, uintptr_t);

	if (task)
		*task = tcb->self;

	threadobj_start(&tcb->thobj); /* We won't wait in prologue. */
	ret = task_prologue(tcb);
	if (ret) {
		delete_tcb(tcb);
		goto out;
	}

	memset(&param, 0, sizeof(param));
	param.sched_priority = prio;
	policy = prio ? SCHED_RT : SCHED_OTHER;
	ret = __bt(-__RT(pthread_setschedparam(pthread_self(), policy, &param)));
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_task_set_periodic(RT_TASK *task, RTIME idate, RTIME period)
{
	struct timespec its, pts;
	struct alchemy_task *tcb;
	struct service svc;
	int ret;

	COPPERPLATE_PROTECT(svc);

	tcb = find_alchemy_task_or_self(task, &ret);
	if (tcb == NULL)
		goto out;

	clockobj_ticks_to_timespec(&alchemy_clock, idate, &its);
	clockobj_ticks_to_timespec(&alchemy_clock, period, &pts);
	/*
	 * We may be scheduled out as a result of this call, so we
	 * can't grab the target thread lock. However, since
	 * threadobj_set_periodic() has to be called lock-free, we
	 * expect it to be robust and properly deal with cancellation
	 * points (COPPERPLATE_PROTECT() put us in deferred mode).
	 */
	ret = threadobj_set_periodic(&tcb->thobj, &its, &pts);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_task_wait_period(unsigned long *overruns_r)
{
	struct alchemy_task *tcb;

	tcb = alchemy_task_current();
	if (tcb == NULL)
		return -EPERM;

	return threadobj_wait_period(&tcb->thobj, overruns_r);
}

int rt_task_sleep(RTIME delay)
{
	struct timespec ts;

	if (threadobj_async_p())
		return -EPERM;

	if (delay == 0)
		return 0;

	clockobj_ticks_to_timeout(&alchemy_clock, delay, &ts);

	return threadobj_sleep(&ts);
}

int rt_task_sleep_until(RTIME date)
{
	struct timespec ts;
	struct service svc;
	ticks_t now;

	if (threadobj_async_p())
		return -EPERM;

	if (date == TM_INFINITE) {
		ts.tv_sec = (time_t)-1 >> 1;
		ts.tv_nsec = 999999999;
	} else {
		COPPERPLATE_PROTECT(svc);
		clockobj_get_time(&alchemy_clock, &now, NULL);
		if (date <= now) {
			COPPERPLATE_UNPROTECT(svc);
			return -ETIMEDOUT;
		}
		clockobj_ticks_to_timespec(&alchemy_clock, date, &ts);
		COPPERPLATE_UNPROTECT(svc);
	}

	return threadobj_sleep(&ts);
}

int rt_task_spawn(RT_TASK *task, const char *name,
		  int stksize, int prio, int mode,
		  void (*entry)(void *arg),
		  void *arg)
{
	int ret;

	ret = rt_task_create(task, name, stksize, prio, mode);
	if (ret)
		return ret;

	return rt_task_start(task, entry, arg);
}

int rt_task_same(RT_TASK *task1, RT_TASK *task2)
{
	return task1->handle == task2->handle;
}

int rt_task_suspend(RT_TASK *task)
{
	struct alchemy_task *tcb;
	struct service svc;
	int ret;

	tcb = get_alchemy_task_or_self(task, &ret);
	if (tcb == NULL)
		return ret;

	COPPERPLATE_PROTECT(svc);
	ret = threadobj_suspend(&tcb->thobj);
	COPPERPLATE_UNPROTECT(svc);
	put_alchemy_task(tcb);

	return ret;
}

int rt_task_resume(RT_TASK *task)
{
	struct alchemy_task *tcb;
	struct service svc;
	int ret;

	tcb = get_alchemy_task(task, &ret);
	if (tcb == NULL)
		return ret;

	COPPERPLATE_PROTECT(svc);
	ret = threadobj_resume(&tcb->thobj);
	COPPERPLATE_UNPROTECT(svc);
	put_alchemy_task(tcb);

	return ret;
}

RT_TASK *rt_task_self(void)
{
	struct alchemy_task *tcb;

	tcb = alchemy_task_current();
	if (tcb == NULL)
		return NULL;

	return &tcb->self;
}

int rt_task_set_priority(RT_TASK *task, int prio)
{
	struct alchemy_task *tcb;
	int ret;

	ret = check_task_priority(prio);
	if (ret)
		return ret;

	tcb = get_alchemy_task_or_self(task, &ret);
	if (tcb == NULL)
		return ret;

	ret = threadobj_set_priority(&tcb->thobj, prio);
	put_alchemy_task(tcb);

	return ret;
}

int rt_task_yield(void)
{
	if (threadobj_async_p())
		return -EPERM;

	threadobj_yield();

	return 0;
}

int rt_task_unblock(RT_TASK *task)
{
	struct alchemy_task *tcb;
	struct service svc;
	int ret;

	tcb = get_alchemy_task(task, &ret);
	if (tcb == NULL)
		return ret;

	COPPERPLATE_PROTECT(svc);
	ret = threadobj_unblock(&tcb->thobj);
	COPPERPLATE_UNPROTECT(svc);
	put_alchemy_task(tcb);

	return ret;
}

int rt_task_slice(RT_TASK *task, RTIME quantum)
{
	struct alchemy_task *tcb;
	struct timespec slice;
	struct service svc;
	int ret;

	clockobj_ticks_to_timespec(&alchemy_clock, quantum, &slice);

	tcb = get_alchemy_task_or_self(task, &ret);
	if (tcb == NULL)
		return ret;

	COPPERPLATE_PROTECT(svc);
	ret = threadobj_set_rr(&tcb->thobj, &slice);
	COPPERPLATE_UNPROTECT(svc);
	put_alchemy_task(tcb);

	return ret;
}

int rt_task_set_mode(int clrmask, int setmask, int *mode_r)
{
	struct alchemy_task *tcb;
	struct service svc;
	int ret = 0;

	if (threadobj_async_p()) {
		clrmask &= ~T_LOCK;
		setmask &= ~T_LOCK;
		return (clrmask | setmask) ? -EPERM : 0;
	}

	if (((clrmask | setmask) & ~(T_LOCK | T_WARNSW | T_CONFORMING)) != 0)
		return -EINVAL;

	tcb = get_alchemy_task_or_self(NULL, &ret);
	if (tcb == NULL)
		return ret;

	COPPERPLATE_PROTECT(svc);
	ret = threadobj_set_mode(&tcb->thobj, clrmask, setmask, mode_r);
	COPPERPLATE_UNPROTECT(svc);

	put_alchemy_task(tcb);

	return ret;
}

int rt_task_inquire(RT_TASK *task, RT_TASK_INFO *info)
{
	struct alchemy_task *tcb;
	struct service svc;
	int ret = 0;

	tcb = get_alchemy_task_or_self(NULL, &ret);
	if (tcb == NULL)
		return ret;

	COPPERPLATE_PROTECT(svc);

	ret = __bt(threadobj_stat(&tcb->thobj, &info->stat));
	if (ret)
		goto out;

	strcpy(info->name, tcb->name);
	info->prio = threadobj_get_priority(&tcb->thobj);
out:
	COPPERPLATE_UNPROTECT(svc);
	put_alchemy_task(tcb);

	return ret;
}

int rt_task_bind(RT_TASK *task,
		 const char *name, RTIME timeout)
{
	return __alchemy_bind_object(name,
				     &alchemy_task_table,
				     timeout,
				     offsetof(struct alchemy_task, cobj),
				     &task->handle);
}

int rt_task_unbind(RT_TASK *task)
{
	*task = no_alchemy_task;
	return 0;
}

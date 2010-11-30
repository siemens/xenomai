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
#include <sched.h>
#include <limits.h>
#include <copperplate/panic.h>
#include <copperplate/heapobj.h>
#include <copperplate/threadobj.h>
#include <copperplate/syncobj.h>
#include <copperplate/clockobj.h>
#include <copperplate/cluster.h>
#include <psos/psos.h>
#include "task.h"
#include "tm.h"

struct cluster psos_task_table;

static unsigned long anon_tids;

static struct psos_task *find_psos_task(u_long tid, int *err_r)
{
	struct psos_task *task = mainheap_deref(tid, struct psos_task);
	unsigned int magic;

	/*
	 * Best-effort to validate a TCB pointer the cheap way,
	 * without relying on any syscall.
	 */
	if (task == NULL || ((uintptr_t)task & (sizeof(uintptr_t)-1)) != 0)
		goto objid_error;

	magic = threadobj_get_magic(&task->thobj);

	if (magic == task_magic)
		return task;

	if (magic == ~task_magic) {
		*err_r = ERR_OBJDEL;
		return NULL;
	}

	if ((magic >> 16) == 0x8181) {
		*err_r = ERR_OBJTYPE;
		return NULL;
	}

objid_error:
	*err_r = ERR_OBJID;

	return NULL;
}

static struct psos_task *find_psos_task_or_self(u_long tid, int *err_r)
{
	struct psos_task *current;

	if (tid)
		return find_psos_task(tid, err_r);

	current = psos_task_current();
	if (current == NULL) {
		*err_r = ERR_SSFN;
		return NULL;
	}
		
	return current;
}

struct psos_task *get_psos_task(u_long tid, int *err_r)
{
	struct psos_task *task = find_psos_task(tid, err_r);

	/*
	 * Grab the task lock, assuming that the task might have been
	 * deleted, and/or maybe we have been lucky, and some random
	 * opaque pointer might lead us to something which is laid in
	 * valid memory but certainly not to a task object. Last
	 * chance is pthread_mutex_lock() detecting a wrong mutex kind
	 * and bailing out.
	 *
	 * XXX: threadobj_lock() disables cancellability for the
	 * caller upon success, until the lock is dropped in
	 * threadobj_unlock(), so there is no way it may vanish while
	 * holding the lock. Therefore we need no cleanup handler
	 * here.
	 */
	if (task == NULL || threadobj_lock(&task->thobj) == -EINVAL)
		return NULL;

	/* Check the magic word again, while we hold the lock. */
	if (threadobj_get_magic(&task->thobj) != task_magic) {
		threadobj_unlock(&task->thobj);
		*err_r = ERR_OBJDEL;
		return NULL;
	}

	return task;
}

struct psos_task *get_psos_task_or_self(u_long tid, int *err_r)
{
	struct psos_task *current;

	if (tid)
		return get_psos_task(tid, err_r);

	current = psos_task_current();
	if (current == NULL) {
		*err_r = ERR_SSFN;
		return NULL;
	}

	/* This one might block but can't fail, it is ours. */
	threadobj_lock(&current->thobj);

	/* Check the magic word again, while we hold the lock. */
	if (threadobj_get_magic(&current->thobj) != task_magic) {
		threadobj_unlock(&current->thobj);
		*err_r = ERR_OBJDEL;
		return NULL;
	}

	return current;
}

void put_psos_task(struct psos_task *task)
{
	threadobj_unlock(&task->thobj);
}

static void task_finalizer(struct threadobj *thobj)
{
	struct psos_task *task = container_of(thobj, struct psos_task, thobj);
	struct syncstate syns;
	struct psos_tm *tm;

	threadobj_lock(&task->thobj);
	threadobj_set_magic(&task->thobj, ~task_magic); /* In case of normal exit. */
	cluster_delobj(&psos_task_table, &task->cobj);

	pvlist_for_each_entry(tm, &task->timer_list, link)
		tm_cancel(mainheap_ref(tm, u_long));

	threadobj_unlock(&task->thobj);

	sem_destroy(&task->barrier);
	/* We have to hold a lock on a syncobj to destroy it. */
	syncobj_lock(&task->sobj, &syns);
	syncobj_destroy(&task->sobj, &syns);
	threadobj_destroy(&task->thobj);

	xnfree(task);
}

static void *task_trampoline(void *arg)
{
	struct psos_task *task = arg;
	struct psos_task_args *args = &task->args;
	struct service svc;
	int ret;

	ret = threadobj_prologue(&task->thobj, task->name);
	if (ret) {
		warning("task %s prologue failed (errno=%d)",
			task->name, -ret);
		goto done;
	}

	COPPERPLATE_PROTECT(svc);

	if (cluster_addobj(&psos_task_table, task->name, &task->cobj)) {
		warning("duplicate task name: %s", task->name);
		/* Make sure we won't un-hash the previous one. */
		strcpy(task->name, "(dup)");
	}

	/* Wait for someone to run t_start() upon us. */
	sem_wait(&task->barrier);

	threadobj_lock(&task->thobj);

	if (task->mode & T_NOPREEMPT)
		threadobj_lock_sched(&task->thobj);

	if (task->mode & T_TSLICE)
		threadobj_set_rr(&task->thobj, &psos_rrperiod);

	threadobj_unlock(&task->thobj);

	COPPERPLATE_UNPROTECT(svc);

	args->entry(args->arg0, args->arg1, args->arg2, args->arg3);
done:

	pthread_exit((void *)(long)ret);
}

static int check_task_priority(u_long psos_prio)
{
	if (psos_prio < 1 || psos_prio > 255) /* In theory. */
		return ERR_PRIOR;

	if ((int)psos_prio >= threadobj_max_prio - 1) /* In practice. */
		panic("current implementation restricts pSOS "
		      "priority levels to range [1..%d]",
		      threadobj_max_prio - 2);

	return SUCCESS;
}

u_long t_create(const char *name, u_long prio,
		u_long sstack, u_long ustack, u_long flags, u_long *tid_r)
{
	struct threadobj_init_data idata;
	struct sched_param param;
	struct psos_task *task;
	pthread_attr_t thattr;
	struct service svc;
	int ret;

	ret = check_task_priority(prio);
	if (ret)
		return ret;

	COPPERPLATE_PROTECT(svc);

	task = xnmalloc(sizeof(struct psos_task));
	if (task == NULL) {
		ret = ERR_NOTCB;
		goto out;
	}

	ustack += sstack;

	/*
	 * Make sure we are granted a minimal amount of stack space
	 * for common usage of the Glibc. If zero, we will pick a
	 * value based on the implementation default for such minimum.
	 */
	if (ustack > 0 && ustack < 8192) {
		xnfree(task);
		ret = ERR_TINYSTK;
		goto out;
	}

	if (name == NULL || *name == '\0')
		sprintf(task->name, "t%lu", ++anon_tids);
	else {
		strncpy(task->name, name, sizeof(task->name));
		task->name[sizeof(task->name) - 1] = '\0';
	}

	sem_init(&task->barrier, sem_scope_attribute, 0);
	task->flags = flags;	/* We don't do much with those. */
	task->mode = 0;	/* Not yet known. */
	task->events = 0;
	syncobj_init(&task->sobj, 0, fnref_null);
	memset(task->notepad, 0, sizeof(task->notepad));
	pvlist_init(&task->timer_list);
	*tid_r = mainheap_ref(task, u_long);

	pthread_attr_init(&thattr);

	if (ustack == 0)
		ustack = PTHREAD_STACK_MIN * 4;
	else if (ustack < PTHREAD_STACK_MIN)
		ustack = PTHREAD_STACK_MIN;

	memset(&param, 0, sizeof(param));
	param.sched_priority = psos_task_normalize_priority(prio);
	pthread_attr_setinheritsched(&thattr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&thattr, SCHED_FIFO);
	pthread_attr_setschedparam(&thattr, &param);
	pthread_attr_setstacksize(&thattr, ustack);
	pthread_attr_setscope(&thattr, thread_scope_attribute);

	idata.magic = task_magic;
	idata.wait_hook = NULL;
	idata.suspend_hook = NULL;
	idata.finalizer = task_finalizer;
	threadobj_init(&task->thobj, &idata);

	ret = pthread_create(&task->thobj.tid, &thattr, &task_trampoline, task);
	pthread_attr_destroy(&thattr);
	if (ret) {
		sem_destroy(&task->barrier);
		xnfree(task);
		ret = ERR_NOTCB;
	}
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

u_long t_start(u_long tid,
	       u_long mode,
	       void (*entry)(u_long, u_long, u_long, u_long),
	       u_long args[])
{
	struct psos_task *task;
	int ret;

	task = get_psos_task(tid, &ret);
	if (task == NULL)
		return ret;

	task->args.entry = entry;
	task->args.arg0 = args[0];
	task->args.arg1 = args[1];
	task->args.arg2 = args[2];
	task->args.arg3 = args[3];
	task->mode = mode;
	put_psos_task(task);
	/* Assume sem_post() will check for validity. */
	sem_post(&task->barrier);

	return SUCCESS;
}

u_long t_suspend(u_long tid)
{
	struct psos_task *task;
	int ret;

	task = get_psos_task_or_self(tid, &ret);
	if (task == NULL)
		return ret;

	ret = threadobj_suspend(&task->thobj);
	put_psos_task(task);

	if (ret)
		return ERR_OBJDEL;

	return SUCCESS;
}

u_long t_resume(u_long tid)
{
	struct psos_task *task;
	int ret;

	task = get_psos_task(tid, &ret);
	if (task == NULL)
		return ret;

	ret = threadobj_resume(&task->thobj);
	put_psos_task(task);

	if (ret)
		return ERR_OBJDEL;

	return SUCCESS;
}

u_long t_setpri(u_long tid, u_long newprio, u_long *oldprio_r)
{
	struct psos_task *task;
	int ret, pprio;

	task = get_psos_task_or_self(tid, &ret);
	if (task == NULL)
		return ret;

	*oldprio_r = threadobj_get_priority(&task->thobj);

	if (newprio == 0) { /* Only inquires for the task priority. */
		put_psos_task(task);
		return SUCCESS;
	}

	ret = check_task_priority(newprio);
	if (ret) {
		put_psos_task(task);
		return ERR_SETPRI;
	}

	pprio = psos_task_normalize_priority(newprio);
	ret = threadobj_set_priority(&task->thobj, pprio);
	put_psos_task(task);
	if (ret)
		return ERR_OBJDEL;

	return SUCCESS;
}

u_long t_delete(u_long tid)
{
	struct psos_task *task;
	int ret;

	task = find_psos_task_or_self(tid, &ret);
	if (task == NULL)
		return ret;

	if (task == psos_task_current()) /* Self-deletion. */
		pthread_exit(NULL);

	threadobj_lock(&task->thobj);
	/*
	 * This basically makes the thread enter a zombie state, since
	 * it won't be reachable by anyone after its magic has been
	 * trashed.
	 */
	threadobj_set_magic(&task->thobj, ~task_magic);
	threadobj_unlock(&task->thobj);

	ret = threadobj_cancel(&task->thobj);
	if (ret)
		return ERR_OBJDEL;

	return SUCCESS;
}

u_long t_ident(const char *name, u_long node, u_long *tid_r)
{
	struct clusterobj *cobj;
	struct psos_task *task;
	struct service svc;

	if (node)
		return ERR_NODENO;

	COPPERPLATE_PROTECT(svc);
	cobj = cluster_findobj(&psos_task_table, name);
	COPPERPLATE_UNPROTECT(svc);
	if (cobj == NULL)
		return ERR_OBJNF;

	task = container_of(cobj, struct psos_task, cobj);
	*tid_r = mainheap_ref(task, u_long);

	return SUCCESS;
}

u_long t_getreg(u_long tid, u_long regnum, u_long *regvalue_r)
{
	struct psos_task *task;
	int ret;

	if (regnum >= PSOSTASK_NR_REGS)
		return ERR_REGNUM;

	task = get_psos_task_or_self(tid, &ret);
	if (task == NULL)
		return ret;

	*regvalue_r = task->notepad[regnum];
	put_psos_task(task);

	return SUCCESS;
}

u_long t_setreg(u_long tid, u_long regnum, u_long regvalue)
{
	struct psos_task *task;
	int ret;

	if (regnum >= PSOSTASK_NR_REGS)
		return ERR_REGNUM;

	task = get_psos_task_or_self(tid, &ret);
	if (task == NULL)
		return ret;

	task->notepad[regnum] = regvalue;
	put_psos_task(task);

	return SUCCESS;
}

u_long t_mode(u_long mask, u_long newmask, u_long *oldmode_r)
{
	struct psos_task *task;
	int ret;

	task = get_psos_task_or_self(0, &ret);
	if (task == NULL)
		return ret;

	*oldmode_r = task->mode;

	if (mask == 0)
		goto done;

	task->mode &= ~mask;
	task->mode |= (newmask & mask);

	if (task->mode & T_TSLICE)
		threadobj_set_rr(&task->thobj, &psos_rrperiod);
	else
		threadobj_set_rr(&task->thobj, NULL);

	if (task->mode & T_NOPREEMPT)
		threadobj_lock_sched_once(&task->thobj);
	else if (*oldmode_r & T_NOPREEMPT)
		threadobj_unlock_sched(&task->thobj);
done:
	put_psos_task(task);

	return SUCCESS;
}

static int collect_events(struct psos_task *task,
			  u_long flags, u_long events, u_long *events_r)
{
	if (((flags & EV_ANY) && (events & task->events) != 0) ||
	    (!(flags & EV_ANY) && ((events & task->events) == events))) {
		/*
		 * The condition is satisfied; update the return value
		 * with the set of matched events, and clear the
		 * collected events from the task's mask.
		 */
		*events_r = (task->events & events);
		task->events &= ~events;
		return 1;
	}

	return 0;
}

u_long ev_receive(u_long events, u_long flags,
		  u_long timeout, u_long *events_r)
{
	struct timespec ts, *timespec;
	struct psos_task *current;
	struct syncstate syns;
	struct service svc;
	int ret;

	current = get_psos_task_or_self(0, &ret);
	if (current == NULL)
		return ret;

	ret = syncobj_lock(&current->sobj, &syns);
	/*
	 * We can't be cancelled asynchronously while we hold a
	 * syncobj lock, so we may safely drop our thread lock now.
	 */
	put_psos_task(current);
	if (ret)
		return ERR_OBJDEL;

	COPPERPLATE_PROTECT(svc);

	if (events == 0) {
		*events_r = current->events; /* Only polling events. */
		goto done;
	}

	if (collect_events(current, flags, events, events_r))
		goto done;

	if (flags & EV_NOWAIT) {
		ret = ERR_NOEVS;
		goto done;
	}

	if (timeout != 0) {
		timespec = &ts;
		clockobj_ticks_to_timeout(&psos_clock, timeout, timespec);
	} else
		timespec = NULL;

	for (;;) {
		ret = syncobj_pend(&current->sobj, timespec, &syns);
		if (ret == -ETIMEDOUT) {
			ret = ERR_TIMEOUT;
			break;
		}
		if (collect_events(current, flags, events, events_r))
			break;
	}
done:
	syncobj_unlock(&current->sobj, &syns);

	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

u_long ev_send(u_long tid, u_long events)
{
	struct psos_task *task;
	struct syncstate syns;
	struct service svc;
	int ret = SUCCESS;

	task = get_psos_task(tid, &ret);
	if (task == NULL)
		return ret;

	COPPERPLATE_PROTECT(svc);

	ret = syncobj_lock(&task->sobj, &syns);
	put_psos_task(task);
	if (ret) {
		ret = ERR_OBJDEL;
		goto out;
	}

	task->events |= events;
	/*
	 * If the task is pending in ev_receive(), it's likely that we
	 * are posting events the task is waiting for, so we can wake
	 * it up immediately and let it confirm whether the condition
	 * is now satisfied.
	 */
	syncobj_post(&task->sobj);

	syncobj_unlock(&task->sobj, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

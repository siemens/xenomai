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

#include <sched.h>
#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "copperplate/heapobj.h"
#include "copperplate/internal.h"
#include "internal.h"
#include "task.h"
#include "buffer.h"
#include "queue.h"
#include "timer.h"
#include "heap.h"

union alchemy_wait_union {
	struct alchemy_task_wait task_wait;
	struct alchemy_buffer_wait buffer_wait;
	struct alchemy_queue_wait queue_wait;
	struct alchemy_heap_wait heap_wait;
};

struct syncluster alchemy_task_table;

static struct alchemy_namegen task_namegen = {
	.prefix = "task",
	.length = sizeof ((struct alchemy_task *)0)->name,
};

static void delete_tcb(struct alchemy_task *tcb);

static struct alchemy_task *find_alchemy_task(RT_TASK *task, int *err_r)
{
	struct alchemy_task *tcb;

	if (bad_pointer(task))
		goto bad_handle;

	tcb = mainheap_deref(task->handle, struct alchemy_task);
	if (bad_pointer(tcb))
		goto bad_handle;

	if (threadobj_get_magic(&tcb->thobj) == task_magic)
		return tcb;
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
	if (tcb == NULL || threadobj_lock(&tcb->thobj) == -EINVAL) {
		*err_r = -EINVAL;
		return NULL;
	}

	/* Check the magic word again, while we hold the lock. */
	if (threadobj_get_magic(&tcb->thobj) != task_magic) {
		threadobj_unlock(&tcb->thobj);
		*err_r = -EINVAL;
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
	/*
	 * Both the safe and msg syncs may be pended by other threads,
	 * so we do have to use syncobj_destroy() for them (i.e. NOT
	 * syncobj_uninit()).
	 */
	__bt(syncobj_lock(&tcb->sobj_safe, &syns));
	syncobj_destroy(&tcb->sobj_safe, &syns);
	__bt(syncobj_lock(&tcb->sobj_msg, &syns));
	syncobj_destroy(&tcb->sobj_msg, &syns);
	threadobj_destroy(&tcb->thobj);
	backtrace_dump(&thobj->btd);

	threadobj_free(tcb);
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

	threadobj_wait_start();

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

	threadobj_notify_entry();
	tcb->entry(tcb->arg);
out:
	threadobj_lock(&tcb->thobj);
	threadobj_set_magic(&tcb->thobj, ~task_magic);
	threadobj_unlock(&tcb->thobj);

	pthread_exit((void *)(long)ret);
}

static int create_tcb(struct alchemy_task **tcbp, RT_TASK *task,
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

	tcb = threadobj_alloc(struct alchemy_task, thobj,
			      union alchemy_wait_union);
	if (tcb == NULL)
		return -ENOMEM;

	alchemy_build_name(tcb->name, name, &task_namegen);

	tcb->mode = mode;
	tcb->entry = NULL;	/* Not yet known. */
	tcb->arg = NULL;

	CPU_ZERO(&tcb->affinity);
	for (cpu = 0; cpu < 8; cpu++) {
		if (mode & T_CPU(cpu))
			CPU_SET(cpu, &tcb->affinity);
	}

	tcb->safecount = 0;
	syncobj_init(&tcb->sobj_safe, 0, fnref_null);
	syncobj_init(&tcb->sobj_msg, SYNCOBJ_PRIO, fnref_null);
	tcb->flowgen = 0;

	idata.magic = task_magic;
	idata.wait_hook = NULL;
	idata.suspend_hook = NULL;
	idata.finalizer = task_finalizer;
	idata.priority = prio;
	threadobj_init(&tcb->thobj, &idata);

	*tcbp = tcb;

	/*
	 * CAUTION: The task control block must be fully built before
	 * we publish it through syncluster_addobj(), at which point
	 * it could be referred to immediately from another task as we
	 * got preempted. In addition, the task descriptor must be
	 * updated prior to starting the task.
	 */
	tcb->self.handle = mainheap_ref(tcb, uintptr_t);

	if (syncluster_addobj(&alchemy_task_table, tcb->name, &tcb->cobj)) {
		delete_tcb(tcb);
		return -EEXIST;
	}

	if (task)
		task->handle = tcb->self.handle;

	return 0;
}

static void delete_tcb(struct alchemy_task *tcb)
{
	threadobj_destroy(&tcb->thobj);
	syncobj_uninit(&tcb->sobj_safe);
	syncobj_uninit(&tcb->sobj_msg);
	threadobj_free(tcb);
}

/**
 * @fn int rt_task_create(RT_TASK *task, const char *name, int stksize, int prio, int mode)
 * @brief Create a real-time task.
 *
 * This service creates a task with access to the full set of Xenomai
 * real-time services. If @a prio is non-zero, the new task belongs to
 * Xenomai's real-time FIFO scheduling class, aka SCHED_RT. If @a prio
 * is zero, the task belongs to the regular SCHED_OTHER class.
 *
 * Creating tasks with zero priority is useful for running non
 * real-time processes which may invoke blocking real-time services,
 * such as pending on a semaphore, reading from a message queue or a
 * buffer, and so on.
 *
 * Once created, the task is left dormant until it is actually started
 * by rt_task_start().
 *
 * @param task The address of a task descriptor which can be later
 * used to identify uniquely the created object, upon success of this
 * call.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * task. When non-NULL and non-empty, a copy of this string is
 * used for indexing the created task into the object registry.
 *
 * @param stksize The size of the stack (in bytes) for the new
 * task. If zero is passed, a system-dependent default size will be
 * substituted.
 *
 * @param prio The base priority of the new task. This value must be
 * in the [0 .. 99] range, where 0 is the lowest effective priority. 
 *
 * @param mode The task creation mode. The following flags can be
 * OR'ed into this bitmask, each of them affecting the new task:
 *
 * - T_SUSP causes the task to start in suspended mode; a call to
 * rt_task_resume() is required to actually start its execution.
 *
 * - T_CPU(cpuid) makes the new task affine to CPU # @b cpuid. CPU
 * identifiers range from 0 to 7 (inclusive).
 *
 * - T_JOINABLE allows another task to wait on the termination of the
 * new task. rt_task_join() shall be called for this task to clean up
 * any resources after its termination.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if either @a prio, @a mode or @a stksize are
 * invalid.
 *
 * - -ENOMEM is returned if the system fails to get memory from the
 * main heap in order to create the task.
 *
 * - -EEXIST is returned if the @a name is conflicting with an already
 * registered task.
 *
 * - -EPERM is returned if this service was called from an
 * asynchronous context.
 *
 * Valid calling context:
 *
 * - Regular POSIX threads
 * - Xenomai threads
 *
 * Core specifics:
 *
 * When running over the Cobalt core:
 *
 * - calling rt_task_create() causes SCHED_RT tasks to switch to
 * secondary mode.
 *
 * - members of Xenomai's SCHED_RT class running in the primary domain
 * have utmost priority over all Linux activities in the system,
 * including regular interrupt handlers.
 *
 * When running over the Mercury core, the SCHED_RT class is mapped
 * over the regular POSIX SCHED_FIFO class.
 *
 * @note Tasks can be referred to from multiple processes which all
 * belong to the same Xenomai session.
 */
int rt_task_create(RT_TASK *task, const char *name,
		   int stksize, int prio, int mode)
{
	struct alchemy_task *tcb;
	struct service svc;
	int ret;

	COPPERPLATE_PROTECT(svc);

	ret = create_tcb(&tcb, task, name, prio, mode);
	if (ret)
		goto out;

	/* We want this to be set prior to spawning the thread. */
	tcb->self = *task;

	ret = __bt(copperplate_create_thread(prio, task_trampoline, tcb,
					     stksize, &tcb->thobj.tid));
	if (ret)
		delete_tcb(tcb);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

/**
 * @fn int rt_task_delete(RT_TASK *task)
 * @brief Delete a real-time task.
 *
 * This call terminates a task previously created by
 * rt_task_create().
 *
 * Tasks created with the T_JOINABLE flag shall be joined by a
 * subsequent call to rt_task_join() once successfully deleted, to
 * reclaim all resources.
 *
 * @param task The descriptor address of the deleted task.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a task is not a valid task descriptor.
 *
 * - -EIDRM is returned if @a task is deleted while the caller was
 * waiting for the target task to exit a safe section.
 *
 * - -EPERM is returned if this service was called from an
 * asynchronous context.
 *
 * Valid calling context:
 *
 * - Regular POSIX threads
 * - Xenomai threads
 *
 * @note rt_task_delete() may block until the deleted task exits a
 * safe section, previously entered by a call to rt_task_safe().
 */
int rt_task_delete(RT_TASK *task)
{
	struct alchemy_task *tcb;
	struct syncstate syns;
	struct service svc;
	int ret;

	if (threadobj_irq_p())
		return -EPERM;

	tcb = find_alchemy_task_or_self(task, &ret);
	if (tcb == NULL)
		return ret;

	if (tcb == alchemy_task_current()) /* Self-deletion. */
		pthread_exit(NULL);

	COPPERPLATE_PROTECT(svc);

	threadobj_lock(&tcb->thobj);
	/*
	 * Prevent further reference to this zombie, including via
	 * alchemy_task_current().
	 */
	threadobj_set_magic(&tcb->thobj, ~task_magic);
	threadobj_unlock(&tcb->thobj);

	if (syncobj_lock(&tcb->sobj_safe, &syns)) {
		ret = -EIDRM;
		goto out;
	}

	while (tcb->safecount) {
		ret = syncobj_wait_grant(&tcb->sobj_safe, NULL, &syns);
		if (ret) {
			if (ret == -EIDRM)
				goto out;

			syncobj_unlock(&tcb->sobj_safe, &syns);
			return ret;
		}
	}

	syncobj_unlock(&tcb->sobj_safe, &syns);

	threadobj_lock(&tcb->thobj);

	ret = threadobj_cancel(&tcb->thobj);
	if (ret)
		ret = -EIDRM;
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

/**
 * @fn int rt_task_start(RT_TASK *task, void (*entry)(void *arg), void *arg)
 * @brief Start a real-time task.
 *
 * This call starts execution of a task previously created by
 * rt_task_create(). This service causes the started task to leave the
 * initial dormant state.
 *
 * @param task The descriptor address of the task to be started.
 *
 * @param entry The address of the task entry point.
 *
 * @param arg A user-defined opaque argument @a entry will receive.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a task is not a valid task descriptor.
 *
 * Valid calling context: any.
 *
 * @note Starting an already started task leads to a nop, returning a
 * success status.
 */
int rt_task_start(RT_TASK *task,
		  void (*entry)(void *arg),
		  void *arg)
{
	struct alchemy_task *tcb;
	struct service svc;
	int ret = 0;

	COPPERPLATE_PROTECT(svc);

	tcb = get_alchemy_task(task, &ret);
	if (tcb == NULL)
		goto out;

	tcb->entry = entry;
	tcb->arg = arg;
	threadobj_start(&tcb->thobj);
	put_alchemy_task(tcb);
out:
	COPPERPLATE_PROTECT(svc);

	return ret;
}

/**
 * @fn int rt_task_shadow(RT_TASK *task, const char *name, int prio, int mode)
 * @brief Turn caller into a real-time task.
 *
 * Extends the calling Linux task with Xenomai capabilities, with
 * access to the full set of Xenomai real-time services. This service
 * is typically used for turning the main() thread of an application
 * process into a Xenomai-enabled task.
 *
 * If @a prio is non-zero, the new task moves to Xenomai's real-time
 * FIFO scheduling class, aka SCHED_RT. If @a prio is zero, the task
 * moves to the regular SCHED_OTHER class.
 *
 * Running Xenomai tasks with zero priority is useful for running non
 * real-time processes which may invoke blocking real-time services,
 * such as pending on a semaphore, reading from a message queue or a
 * buffer, and so on.
 *
 * Once shadowed with the Xenomai extension, the calling task returns
 * and resumes execution normally from the call site.
 *
 * @param task If non-NULL, the address of a task descriptor which can
 * be later used to identify uniquely the task, upon success of this
 * call. If NULL, no descriptor is returned.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * task. When non-NULL and non-empty, a copy of this string is
 * used for indexing the task into the object registry.
 *
 * @param prio The base priority of the task. This value must be in
 * the [0 .. 99] range, where 0 is the lowest effective priority.
 *
 * @param mode The task creation mode. The following flags can be
 * OR'ed into this bitmask, each of them affecting the new task:
 *
 * - T_SUSP causes the task to suspend before returning from this
 * service; a call to rt_task_resume() is required to resume
 * execution.
 *
 * - T_CPU(cpuid) makes the new task affine to CPU # @b cpuid. CPU
 * identifiers range from 0 to 7 (inclusive).
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if either @a prio or @a mode are invalid.
 *
 * - -ENOMEM is returned if the system fails to get memory from the
 * main heap in order to create the task extension.
 *
 * - -EEXIST is returned if the @a name is conflicting with an already
 * registered task.
 *
 * - -EBUSY is returned if the caller is already mapped to a Xenomai
 * task context.
 *
 * - -EPERM is returned if this service was called from an
 * asynchronous context.
 *
 * Valid calling context:
 *
 * - Regular POSIX threads
 *
 * Core specifics:
 *
 * When running over the Cobalt core:
 *
 * - the caller always returns from this service in primary mode.
 *
 * @note Tasks can be referred to from multiple processes which all
 * belong to the same Xenomai session.
 */
int rt_task_shadow(RT_TASK *task, const char *name, int prio, int mode)
{
	struct threadobj *current = threadobj_current();
	struct alchemy_task *tcb;
	struct service svc;
	int ret;

	COPPERPLATE_PROTECT(svc);

	/*
	 * This is ok to overlay the default TCB for the main thread
	 * assigned by Copperplate at init, but it is not to
	 * over-shadow a Xenomai thread. A valid TCB pointer with a
	 * zero magic identifies the default main TCB.
	 */
	if (current && threadobj_get_magic(current))
		return -EBUSY;

	ret = create_tcb(&tcb, task, name, prio, mode);
	if (ret)
		goto out;

	threadobj_lock(&tcb->thobj);
	threadobj_shadow(&tcb->thobj); /* We won't wait in prologue. */
	threadobj_unlock(&tcb->thobj);
	ret = task_prologue(tcb);
	if (ret) {
		delete_tcb(tcb);
		goto out;
	}

	ret = __bt(copperplate_renice_thread(pthread_self(), prio));
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

/**
 * @fn int rt_task_set_periodic(RT_TASK *task, RTIME idate, RTIME period)
 * @brief Make a real-time task periodic.
 *
 * Make a task periodic by programing its first release point and its
 * period in the processor time line.  @a task should then call
 * rt_task_wait_period() to sleep until the next periodic release
 * point in the processor timeline is reached.
 *
 * @param task The descriptor address of the periodic task.
 * If @a task is NULL, the current task is made periodic.
 *
 * @param idate The initial (absolute) date of the first release
 * point, expressed in clock ticks (see note). @a task will be delayed
 * until this point is reached. If @a idate is equal to TM_NOW, the
 * current system date is used.
 *
 * @param period The period of the task, expressed in clock ticks (see
 * note). Passing TM_INFINITE stops the task's periodic timer if
 * enabled, then returns successfully.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a task is NULL but the caller is not a
 * Xenomai task, or if @a task is non-NULL but not a valid task
 * descriptor.
 *
 * - -ETIMEDOUT is returned if @a idate is different from TM_INFINITE
 * and represents a date in the past.
 *
 * Valid calling contexts:
 *
 * - Xenomai threads
 *
 * Core specifics:
 *
 * Over Cobalt, -EINVAL is returned if @a period is different from
 * TM_INFINITE but shorter than the scheduling latency value for the
 * target system, as available from /proc/xenomai/latency.
 *
 * @note The @a idate and @a period values are interpreted as a
 * multiple of the Alchemy clock resolution (see
 * --alchemy-clock-resolution option, defaults to 1 nanosecond).
 */
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

/**
 * @fn int rt_task_wait_period(unsigned long *overruns_r)
 * @brief Wait for the next periodic release point.
 *
 * Delay the current task until the next periodic release point is
 * reached. The periodic timer should have been previously started for
 * @a task by a call to rt_task_set_periodic().
 *
 * @param overruns_r If non-NULL, @a overruns_r shall be a pointer to
 * a memory location which will be written with the count of pending
 * overruns. This value is written to only when rt_task_wait_period()
 * returns -ETIMEDOUT or success. The memory location remains
 * unmodified otherwise. If NULL, this count will not be returned.
 *
 * @return 0 is returned upon success. If @a overruns_r is non-NULL,
 * zero is written to the pointed memory location. Otherwise:
 *
 * - -EWOULDBLOCK is returned if rt_task_set_periodic() was not called
 * for the current task.
 *
 * - -EINTR is returned if rt_task_unblock() was called for the
 * waiting task before the next periodic release point was reached. In
 * this case, the overrun counter is also cleared.
 *
 * - -ETIMEDOUT is returned if a timer overrun occurred, which
 * indicates that a previous release point was missed by the calling
 * task. If @a overruns_r is non-NULL, the count of pending overruns
 * is written to the pointed memory location.
 *
 * - -EPERM is returned if this service was called from an
 * asynchronous context.
 *
 * @note If the current release point has already been reached at the
 * time of the call, the current task immediately returns from this
 * service with no delay.
 */
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
	struct service svc;

	if (!threadobj_current_p())
		return -EPERM;

	if (delay == 0)
		return 0;

	COPPERPLATE_PROTECT(svc);
	clockobj_ticks_to_timeout(&alchemy_clock, delay, &ts);
	COPPERPLATE_UNPROTECT(svc);

	return threadobj_sleep(&ts);
}

int rt_task_sleep_until(RTIME date)
{
	struct timespec ts;
	ticks_t now;

	if (!threadobj_current_p())
		return -EPERM;

	if (date == TM_INFINITE) {
		ts.tv_sec = (time_t)-1 >> 1;
		ts.tv_nsec = 999999999;
	} else {
		clockobj_get_time(&alchemy_clock, &now, NULL);
		if (date <= now)
			return -ETIMEDOUT;
		clockobj_ticks_to_timespec(&alchemy_clock, date, &ts);
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

	COPPERPLATE_PROTECT(svc);

	tcb = get_alchemy_task_or_self(task, &ret);
	if (tcb == NULL)
		goto out;

	ret = threadobj_suspend(&tcb->thobj);
	put_alchemy_task(tcb);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_task_resume(RT_TASK *task)
{
	struct alchemy_task *tcb;
	struct service svc;
	int ret;

	COPPERPLATE_PROTECT(svc);

	tcb = get_alchemy_task(task, &ret);
	if (tcb == NULL)
		goto out;

	ret = threadobj_resume(&tcb->thobj);
	put_alchemy_task(tcb);
out:
	COPPERPLATE_UNPROTECT(svc);

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
	struct service svc;
	int ret;

	ret = check_task_priority(prio);
	if (ret)
		return ret;

	COPPERPLATE_PROTECT(svc);

	tcb = get_alchemy_task_or_self(task, &ret);
	if (tcb == NULL)
		goto out;

	ret = threadobj_set_priority(&tcb->thobj, prio);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_task_yield(void)
{
	if (!threadobj_current_p())
		return -EPERM;

	threadobj_yield();

	return 0;
}

int rt_task_unblock(RT_TASK *task)
{
	struct alchemy_task *tcb;
	struct service svc;
	int ret;

	COPPERPLATE_PROTECT(svc);

	tcb = get_alchemy_task(task, &ret);
	if (tcb == NULL)
		goto out;

	ret = threadobj_unblock(&tcb->thobj);
	put_alchemy_task(tcb);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_task_slice(RT_TASK *task, RTIME quantum)
{
	struct alchemy_task *tcb;
	struct timespec slice;
	struct service svc;
	int ret;

	COPPERPLATE_PROTECT(svc);

	clockobj_ticks_to_timespec(&alchemy_clock, quantum, &slice);

	tcb = get_alchemy_task_or_self(task, &ret);
	if (tcb == NULL)
		goto out;

	ret = threadobj_set_rr(&tcb->thobj, &slice);
	put_alchemy_task(tcb);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_task_set_mode(int clrmask, int setmask, int *mode_r)
{
	struct alchemy_task *tcb;
	struct service svc;
	int ret = 0;

	if (threadobj_irq_p()) {
		clrmask &= ~T_LOCK;
		setmask &= ~T_LOCK;
		return (clrmask | setmask) ? -EPERM : 0;
	}

	if (((clrmask | setmask) & ~(T_LOCK | T_WARNSW | T_CONFORMING)) != 0)
		return -EINVAL;

	COPPERPLATE_PROTECT(svc);

	tcb = get_alchemy_task_or_self(NULL, &ret);
	if (tcb == NULL)
		goto out;

	ret = threadobj_set_mode(&tcb->thobj, clrmask, setmask, mode_r);
	put_alchemy_task(tcb);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_task_inquire(RT_TASK *task, RT_TASK_INFO *info)
{
	struct alchemy_task *tcb;
	struct service svc;
	int ret = 0;

	COPPERPLATE_PROTECT(svc);

	tcb = get_alchemy_task_or_self(task, &ret);
	if (tcb == NULL)
		goto out;

	ret = __bt(threadobj_stat(&tcb->thobj, &info->stat));
	if (ret)
		goto out;

	strcpy(info->name, tcb->name);
	info->prio = threadobj_get_priority(&tcb->thobj);

	put_alchemy_task(tcb);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

ssize_t rt_task_send_timed(RT_TASK *task,
			   RT_TASK_MCB *mcb_s, RT_TASK_MCB *mcb_r,
			   const struct timespec *abs_timeout)
{
	struct alchemy_task_wait *wait;
	struct threadobj *current;
	struct alchemy_task *tcb;
	struct syncstate syns;
	struct service svc;
	ssize_t ret;
	int err;

	current = threadobj_current();
	if (current == NULL)
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	tcb = find_alchemy_task(task, &err);
	if (tcb == NULL) {
		ret = err;
		goto out;
	}

	ret = syncobj_lock(&tcb->sobj_msg, &syns);
	if (ret)
		goto out;

	if (alchemy_poll_mode(abs_timeout)) {
		if (!syncobj_count_drain(&tcb->sobj_msg)) {
			ret = -EWOULDBLOCK;
			goto done;
		}
		abs_timeout = NULL;
	}

	/* Get space for the reply. */
	wait = threadobj_prepare_wait(struct alchemy_task_wait);

	/*
	 * Compute the next flow identifier, making sure that we won't
	 * draw a null or negative value.
	 */
	if (++tcb->flowgen < 0)
		tcb->flowgen = 1;

	wait->request = *mcb_s;
	wait->request.flowid = tcb->flowgen;
	if (mcb_r) {
		wait->reply.data = mcb_r->data;
		wait->reply.size = mcb_r->size;
	} else {
		wait->reply.data = NULL;
		wait->reply.size = 0;
	}

	if (syncobj_count_drain(&tcb->sobj_msg))
		syncobj_drain(&tcb->sobj_msg);

	ret = syncobj_wait_grant(&tcb->sobj_msg, abs_timeout, &syns);
	if (ret) {
		threadobj_finish_wait();
		if (ret == -EIDRM)
			goto out;
		goto done;
	}
	ret = wait->reply.size;

	threadobj_finish_wait();
done:
	syncobj_unlock(&tcb->sobj_msg, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_task_receive_timed(RT_TASK_MCB *mcb_r,
			  const struct timespec *abs_timeout)
{
	struct alchemy_task_wait *wait;
	struct alchemy_task *current;
	struct threadobj *thobj;
	struct syncstate syns;
	struct service svc;
	RT_TASK_MCB *mcb_s;
	int ret;

	current = alchemy_task_current();
	if (current == NULL)
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	__bt(syncobj_lock(&current->sobj_msg, &syns));

	while (!syncobj_grant_wait_p(&current->sobj_msg)) {
		if (alchemy_poll_mode(abs_timeout)) {
			ret = -EWOULDBLOCK;
			goto done;
		}
		syncobj_wait_drain(&current->sobj_msg, abs_timeout, &syns);
	}

	thobj = syncobj_peek_grant(&current->sobj_msg);
	wait = threadobj_get_wait(thobj);
	mcb_s = &wait->request;

	if (mcb_s->size > mcb_r->size) {
		ret = -ENOBUFS;
		goto fixup;
	}

	if (mcb_s->size > 0)
		memcpy(mcb_r->data, mcb_s->data, mcb_s->size);

	/* The flow identifier is always strictly positive. */
	ret = mcb_s->flowid;
	mcb_r->opcode = mcb_s->opcode;
fixup:
	mcb_r->size = mcb_s->size;
done:
	syncobj_unlock(&current->sobj_msg, &syns);

	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_task_reply(int flowid, RT_TASK_MCB *mcb_s)
{
	struct alchemy_task_wait *wait = NULL;
	struct alchemy_task *current;
	struct threadobj *thobj;
	struct syncstate syns;
	struct service svc;
	RT_TASK_MCB *mcb_r;
	size_t size;
	int ret;

	current = alchemy_task_current();
	if (current == NULL)
		return -EPERM;

	if (flowid <= 0)
		return -EINVAL;

	COPPERPLATE_PROTECT(svc);

	__bt(syncobj_lock(&current->sobj_msg, &syns));

	ret = -ENXIO;
	if (!syncobj_grant_wait_p(&current->sobj_msg))
		goto done;

	syncobj_for_each_waiter(&current->sobj_msg, thobj) {
		wait = threadobj_get_wait(thobj);
		if (wait->request.flowid == flowid)
			goto reply;
	}
	goto done;
 reply:
	size = mcb_s ? mcb_s->size : 0;
	syncobj_grant_to(&current->sobj_msg, thobj);
	mcb_r = &wait->reply;

	/*
	 * NOTE: sending back a NULL or zero-length reply is perfectly
	 * valid; it just means to unblock the client without passing
	 * it back any reply data. What is invalid is sending a
	 * response larger than what the client expects.
	 */
	if (mcb_r->size < size) {
		ret = -ENOBUFS;	/* Client will get this too. */
		mcb_r->size = -ENOBUFS;
	} else {
		ret = 0;
		mcb_r->size = size;
		if (size > 0)
			memcpy(mcb_r->data, mcb_s->data, size);
	}

	mcb_r->flowid = flowid;
	mcb_r->opcode = mcb_s ? mcb_s->opcode : 0;
done:
	syncobj_unlock(&current->sobj_msg, &syns);

	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_task_bind(RT_TASK *task,
		 const char *name, RTIME timeout)
{
	return alchemy_bind_object(name,
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

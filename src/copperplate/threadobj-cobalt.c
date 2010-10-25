/*
 * Copyright (C) 2010 Philippe Gerum <rpm@xenomai.org>.
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
 *
 * Thread object abstraction - Cobalt core version.
 */

#include <sys/time.h>
#include <signal.h>
#include <memory.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <limits.h>
#include <sched.h>
#include "copperplate/panic.h"
#include "copperplate/traceobj.h"
#include "copperplate/threadobj.h"
#include "copperplate/syncobj.h"

static DEFINE_PRIVATE_LIST(thread_list);

static pthread_mutex_t list_lock;

pthread_key_t threadobj_tskey;

int threadobj_min_prio;

int threadobj_max_prio;

int threadobj_async;

static int global_rr;

static struct timespec global_quantum;

void threadobj_init(struct threadobj *thobj,
		    struct threadobj_init_data *idata)
{
	pthread_mutexattr_t mattr;
	pthread_condattr_t cattr;

	thobj->magic = idata->magic;
	thobj->tid = 0;
	thobj->tracer = NULL;
	thobj->finalizer = idata->finalizer;
	thobj->wait_hook = idata->wait_hook;
	thobj->schedlock_depth = 0;
	thobj->status = 0;
	holder_init(&thobj->wait_link);
	thobj->suspend_hook = idata->suspend_hook;

	pthread_condattr_init(&cattr);
	pthread_condattr_setpshared(&cattr, mutex_scope_attribute);
	pthread_cond_init(&thobj->wait_sync, &cattr);
	pthread_condattr_destroy(&cattr);

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, mutex_scope_attribute);
	pthread_mutex_init(&thobj->lock, &mattr);
	pthread_mutexattr_destroy(&mattr);
}

int threadobj_prologue(struct threadobj *thobj) /* thobj->lock free */
{
	pthread_mutex_lock(&list_lock);
	pvlist_append(&thobj->thread_link, &thread_list);
	pthread_mutex_unlock(&list_lock);

	thobj->errno_pointer = &errno;
	pthread_setspecific(threadobj_tskey, thobj);

	if (global_rr)
		threadobj_set_rr(thobj, &global_quantum);

	return 0;
}

int threadobj_lock(struct threadobj *thobj)
{
	int ret, otype;

	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &otype);
	ret = -pthread_mutex_lock(&thobj->lock);
	if (ret)
		pthread_setcanceltype(otype, NULL);
	else
		thobj->cancel_type = otype;

	return ret;
}

int threadobj_trylock(struct threadobj *thobj)
{
	int ret, otype;

	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &otype);
	ret = -pthread_mutex_trylock(&thobj->lock);
	if (ret)
		pthread_setcanceltype(otype, NULL);
	else
		thobj->cancel_type = otype;

	return ret;
}

int threadobj_unlock(struct threadobj *thobj)
{
	int ret, otype = thobj->cancel_type;

	ret = -pthread_mutex_unlock(&thobj->lock);
	pthread_setcanceltype(otype, NULL);

	return ret;
}

int threadobj_cancel(struct threadobj *thobj) /* thobj->lock free */
{
	return -pthread_cancel(thobj->tid);
}

void threadobj_finalize(void *p) /* thobj->lock free */
{
	struct threadobj *thobj = p;

	pthread_mutex_lock(&list_lock);
	pvlist_remove(&thobj->thread_link);
	pthread_mutex_unlock(&list_lock);

	if (thobj->tracer)
		traceobj_unwind(thobj->tracer);

	if (thobj->finalizer)
		thobj->finalizer(thobj);
}

void threadobj_destroy(struct threadobj *thobj) /* thobj->lock free */
{
	pthread_mutex_destroy(&thobj->lock);
}

int threadobj_suspend(struct threadobj *thobj) /* thobj->lock held */
{
	pthread_t tid = thobj->tid;
	int ret;

	threadobj_unlock(thobj);
	ret = pthread_kill(tid, SIGSUSP);
	threadobj_lock(thobj);

	return -ret;
}

int threadobj_resume(struct threadobj *thobj) /* thobj->lock held */
{
	pthread_t tid = thobj->tid;
	int ret;

	threadobj_unlock(thobj);
	ret = pthread_kill(tid, SIGRESM);
	threadobj_lock(thobj);

	return -ret;
}

int threadobj_unblock(struct threadobj *thobj) /* thobj->lock held */
{
	pthread_t tid = thobj->tid;
	int ret = 0;

	if (thobj->wait_sobj)	/* Remove PEND (+DELAY timeout) */
		syncobj_flush(thobj->wait_sobj, SYNCOBJ_FLUSHED);
	else
		/* Remove standalone DELAY */
		ret = -pthread_kill(tid, SIGRELS);

	return ret;
}

int threadobj_lock_sched(struct threadobj *thobj) /* thobj->lock held */
{
	assert(thobj == threadobj_current());

	if (thobj->schedlock_depth++ > 0)
		return 0;

	thobj->status |= THREADOBJ_SCHEDLOCK;
	/*
	 * In essence, we can't be scheduled out as a result of
	 * locking the scheduler, so no need to drop the thread lock
	 * across this call.
	 */
	return -pthread_set_mode_np(0, PTHREAD_LOCK_SCHED);
}

int threadobj_unlock_sched(struct threadobj *thobj) /* thobj->lock held */
{
	int ret;

	assert(thobj == threadobj_current());

	/*
	 * Higher layers may not know about the current locking level
	 * and fully rely on us to track it, so we gracefully handle
	 * unbalanced calls here, and let them decide of the outcome
	 * in case of error.
	 */
	if (thobj->schedlock_depth == 0)
		return -EINVAL;

	if (--thobj->schedlock_depth > 0)
		return 0;

	thobj->status &= ~THREADOBJ_SCHEDLOCK;
	threadobj_unlock(thobj);
	ret = pthread_set_mode_np(PTHREAD_LOCK_SCHED, 0);
	threadobj_lock(thobj);

	return -ret;
}

int threadobj_set_priority(struct threadobj *thobj, int prio) /* thobj->lock held */
{
	pthread_t tid = thobj->tid;
	struct sched_param param;
	int ret, policy;

	ret = pthread_getschedparam(tid, &policy, &param);
	if (ret)
		return -ret;

	if (param.sched_priority == prio)
		return 0;

	threadobj_unlock(thobj);
	/*
	 * XXX: as a side effect, resetting SCHED_RR will refill the
	 * time credit for the target thread with the last rrperiod
	 * set.
	 */
	param.sched_priority = prio;
	ret = pthread_setschedparam(tid, policy, &param);
	threadobj_lock(thobj);

	return -ret;
}

int threadobj_get_priority(struct threadobj *thobj) /* thobj->lock held */
{
	struct sched_param param;
	int ret, policy;

	ret = pthread_getschedparam(thobj->tid, &policy, &param);
	if (ret)
		return -ret;

	return param.sched_priority;
}

static int set_rr(struct threadobj *thobj, struct timespec *quantum)
{
	struct sched_param_ex xparam;
	pthread_t tid = thobj->tid;
	struct sched_param param;
	int policy, ret;

	ret = pthread_getschedparam(tid, &policy, &param);
	if (ret)
		return -ret;

	if (quantum == NULL) {
		xparam.sched_rr_quantum.tv_sec = 0;
		xparam.sched_rr_quantum.tv_nsec = 0;
		thobj->status &= ~THREADOBJ_ROUNDROBIN;
		policy = SCHED_FIFO;
	} else {
		xparam.sched_rr_quantum = *quantum;
		if (quantum->tv_sec == 0 && quantum->tv_nsec == 0) {
			thobj->status &= ~THREADOBJ_ROUNDROBIN;
			policy = SCHED_FIFO;
		} else {
			thobj->status |= THREADOBJ_ROUNDROBIN;
			policy = SCHED_RR;
		}
	}

	xparam.sched_priority = param.sched_priority;
	threadobj_unlock(thobj);
	ret = pthread_setschedparam_ex(tid, policy, &xparam);
	threadobj_lock(thobj);

	return -ret;
}

int threadobj_set_rr(struct threadobj *thobj, struct timespec *quantum)
{				/* thobj->lock held if valid */
	int ret;

	if (thobj)
		return set_rr(thobj, quantum);

	global_rr = (quantum != NULL);
	if (global_rr)
		global_quantum = *quantum;

	/*
	 * XXX: Enable round-robin for all threads locally known by
	 * the current process. Round-robin is most commonly about
	 * having multiple threads getting an equal share of time for
	 * running the same bulk of code, so applying this policy
	 * session-wide to multiple Xenomai processes would not make
	 * much sense. I.e. one is better off having all those threads
	 * running within a single process.
	 */
	ret = 0;
	pthread_mutex_lock(&list_lock);

	pvlist_for_each_entry(thobj, &thread_list, thread_link) {
		threadobj_lock(thobj);
		ret = set_rr(thobj, quantum);
		threadobj_unlock(thobj);
		if (ret)
			break;
	}

	pthread_mutex_unlock(&list_lock);

	return ret;
}

int threadobj_start_rr(struct timespec *quantum)
{
	return threadobj_set_rr(NULL, quantum);
}

void threadobj_stop_rr(void)
{
	threadobj_set_rr(NULL, NULL);
}

void threadobj_pkg_init(void)
{
	threadobj_max_prio = sched_get_priority_max(SCHED_FIFO);
	threadobj_min_prio = sched_get_priority_min(SCHED_FIFO);
	threadobj_async = 0;

	/* PI and recursion would be overkill. */
	pthread_mutex_init(&list_lock, NULL);

	if (pthread_key_create(&threadobj_tskey, threadobj_finalize) != 0)
		panic("failed to allocate TSD key");
}

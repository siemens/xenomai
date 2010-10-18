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
 *
 * Thread object abstraction - Mercury core version.
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
#include "copperplate/notifier.h"
#include "copperplate/traceobj.h"
#include "copperplate/threadobj.h"
#include "copperplate/syncobj.h"

pthread_key_t threadobj_tskey;

int threadobj_min_prio;

int threadobj_max_prio;

int threadobj_async;

static DEFINE_PRIVATE_LIST(thread_list);

static pthread_mutex_t list_lock;

static int global_rr;

static struct timespec global_quantum;

static void notifier_callback(const struct notifier *nf)
{
	struct threadobj *current;

	current = container_of(nf, struct threadobj, core.notifier);
	assert(current == threadobj_current());

	if (current->suspend_hook)
		current->suspend_hook(current, THREADOBJ_SUSPEND);

	notifier_wait(nf); /* Wait for threadobj_resume(). */

	if (current->suspend_hook)
		current->suspend_hook(current, THREADOBJ_RESUME);
}

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

int threadobj_prologue(struct threadobj *thobj)
{
	pthread_mutex_lock(&list_lock);
	pvlist_append(&thobj->thread_link, &thread_list);
	pthread_mutex_unlock(&list_lock);

	thobj->errno_pointer = &errno;
	pthread_setspecific(threadobj_tskey, thobj);
	notifier_init(&thobj->core.notifier, notifier_callback, 1);

	if (global_rr)
		threadobj_set_rr(thobj, &global_quantum);

	return 0;
}

int threadobj_cancel(struct threadobj *thobj)
{
	return -pthread_cancel(thobj->tid);
}

void threadobj_finalize(void *p)
{
	struct threadobj *thobj = p;

	pthread_mutex_lock(&list_lock);
	pvlist_remove(&thobj->thread_link);
	pthread_mutex_unlock(&list_lock);

	notifier_destroy(&thobj->core.notifier);

	if (thobj->tracer)
		traceobj_unwind(thobj->tracer);

	if (thobj->finalizer)
		thobj->finalizer(thobj);
}

void threadobj_destroy(struct threadobj *thobj)
{
	pthread_mutex_destroy(&thobj->lock);
}

int threadobj_suspend(struct threadobj *thobj)
{
	int ret;

	threadobj_unlock(thobj); /* FIXME: racy */
	ret = notifier_signal(&thobj->core.notifier);
	threadobj_lock(thobj);

	return ret;
}

int threadobj_resume(struct threadobj *thobj)
{
	int ret;

	threadobj_unlock(thobj); /* FIXME: racy */
	ret = notifier_release(&thobj->core.notifier);
	threadobj_lock(thobj);

	return ret;
}

int threadobj_lock_sched(struct threadobj *thobj)
{
	pthread_t tid = thobj->tid;
	struct sched_param param;
	int policy, ret;

	assert(thobj == threadobj_current());

	if (thobj->schedlock_depth++ > 0)
		return 0;

	ret = pthread_getschedparam(tid, &policy, &param);
	if (ret)
		return -ret;

	thobj->core.prio_unlocked = param.sched_priority;
	thobj->status |= THREADOBJ_SCHEDLOCK;
	param.sched_priority = threadobj_max_prio - 1;

	return -pthread_setschedparam(tid, SCHED_FIFO, &param);
}

int threadobj_unlock_sched(struct threadobj *thobj)
{
	pthread_t tid = thobj->tid;
	struct sched_param param;
	int ret;

	assert(thobj == threadobj_current());

	if (thobj->schedlock_depth == 0)
		return -EINVAL;

	if (--thobj->schedlock_depth > 0)
		return 0;

	thobj->status &= ~THREADOBJ_SCHEDLOCK;
	param.sched_priority = thobj->core.prio_unlocked;
	threadobj_unlock(thobj);
	ret = pthread_setschedparam(tid, SCHED_FIFO, &param);
	threadobj_lock(thobj);

	return -ret;
}

int threadobj_set_priority(struct threadobj *thobj, int prio)
{
	pthread_t tid = thobj->tid;
	struct sched_param param;
	int ret;

	/*
	 * We don't actually change the scheduling priority in case
	 * the target thread holds the scheduler lock, but only record
	 * the level to set when unlocking.
	 */
	if (thobj->status & THREADOBJ_SCHEDLOCK) {
		thobj->core.prio_unlocked = prio;
		return 0;
	}

	threadobj_unlock(thobj);
	/*
	 * Since we released the thread container lock, we now rely on
	 * the pthread interface to recheck the tid for existence.
	 */
	param.sched_priority = prio;
	ret = pthread_setschedparam(tid, SCHED_FIFO, &param);
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

static void roundrobin_handler(int sig)
{
	struct threadobj *current = threadobj_current();

	/*
	 * We do manual round-robin within SCHED_FIFO to allow for
	 * multiple time slices system-wide.
	 */
	if (current && (current->status & THREADOBJ_ROUNDROBIN))
		sched_yield();
}

static inline void set_rr(struct threadobj *thobj, struct timespec *quantum)
{
	if (quantum) {
		thobj->status |= THREADOBJ_ROUNDROBIN;
		thobj->core.tslice = *quantum;
	} else
		thobj->status &= ~THREADOBJ_ROUNDROBIN;
}

int threadobj_set_rr(struct threadobj *thobj, struct timespec *quantum)
{
	if (thobj) {
		set_rr(thobj, quantum);
		return 0;
	}

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
	pthread_mutex_lock(&list_lock);

	pvlist_for_each_entry(thobj, &thread_list, thread_link) {
		threadobj_lock(thobj);
		set_rr(thobj, quantum);
		threadobj_unlock(thobj);
	}

	pthread_mutex_unlock(&list_lock);

	return 0;
}

int threadobj_start_rr(struct timespec *quantum)
{
	struct itimerval value, ovalue;
	struct sigaction sa;
	int ret;

	ret = threadobj_set_rr(NULL, quantum);
	if (ret)
		return ret;

	value.it_interval.tv_sec = quantum->tv_sec;
	value.it_interval.tv_usec = quantum->tv_nsec / 1000;
	
	ret = getitimer(ITIMER_VIRTUAL, &ovalue);
	if (ret == 0 &&
	    value.it_interval.tv_sec == ovalue.it_interval.tv_sec &&
	    value.it_interval.tv_usec == ovalue.it_interval.tv_usec)
		return 0;	/* Already enabled. */

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = roundrobin_handler;
	sigaction(SIGVTALRM, &sa, NULL);

	value.it_value = value.it_interval;
	ret = setitimer(ITIMER_VIRTUAL, &value, NULL);
	if (ret)
		return -errno;

	return 0;
}

void threadobj_stop_rr(void)
{
	struct itimerval value;
	struct sigaction sa;

	threadobj_set_rr(NULL, NULL);

	value.it_value.tv_sec = 0;
	value.it_value.tv_usec = 0;
	value.it_interval = value.it_value;
	
	setitimer(ITIMER_VIRTUAL, &value, NULL);

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_DFL;
	sigaction(SIGVTALRM, &sa, NULL);
}

void threadobj_pkg_init(void)
{
	threadobj_max_prio = sched_get_priority_max(SCHED_FIFO);
	threadobj_min_prio = sched_get_priority_min(SCHED_FIFO);
	threadobj_async = 0;
	global_rr = 0;

	/* PI and recursion would be overkill. */
	pthread_mutex_init(&list_lock, NULL);

	if (pthread_key_create(&threadobj_tskey, threadobj_finalize) != 0)
		panic("failed to allocate TSD key");

	notifier_pkg_init();
}

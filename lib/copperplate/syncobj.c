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

#include <assert.h>
#include <errno.h>
#include "copperplate/lock.h"
#include "copperplate/threadobj.h"
#include "copperplate/syncobj.h"
#include "copperplate/debug.h"
#include "internal.h"

/*
 * XXX: The POSIX spec states that "Synchronization primitives that
 * attempt to interfere with scheduling policy by specifying an
 * ordering rule are considered undesirable. Threads waiting on
 * mutexes and condition variables are selected to proceed in an order
 * dependent upon the scheduling policy rather than in some fixed
 * order (for example, FIFO or priority). Thus, the scheduling policy
 * determines which thread(s) are awakened and allowed to proceed.".
 * Linux enforces this by always queuing SCHED_FIFO waiters by
 * priority when sleeping on futex objects, which underlay mutexes and
 * condition variables.
 *
 * Unfortunately, most non-POSIX RTOS do allow specifying the queuing
 * order which applies to their synchronization objects at creation
 * time, and ignoring the FIFO queuing requirement may break the
 * application in case a fair attribution of the resource is
 * expected. Therefore, we must emulate FIFO ordering, and we do that
 * using an internal queue. We also use this queue to implement the
 * flush operation on synchronization objects which POSIX does not
 * provide either.
 *
 * The syncobj abstraction is based on a complex monitor object to
 * wait for resources, either implemented natively by Cobalt or
 * emulated via a mutex and two condition variables over Mercury (one
 * of which being hosted by the thread object implementation).
 *
 * NOTE: we do no do error backtracing in this file, since error
 * returns when locking, pending or deleting sync objects express
 * normal runtime conditions.
 */

#ifdef CONFIG_XENO_COBALT

#include "cobalt/internal.h"

static inline
int monitor_enter(struct syncobj *sobj)
{
	return cobalt_monitor_enter(&sobj->core.monitor);
}

static inline
void monitor_exit(struct syncobj *sobj)
{
	int ret;
	ret = cobalt_monitor_exit(&sobj->core.monitor);
	assert(ret == 0);
}

static inline
int monitor_wait_grant(struct syncobj *sobj,
		       struct threadobj *current,
		       const struct timespec *timeout)
{
	return cobalt_monitor_wait(&sobj->core.monitor,
				   COBALT_MONITOR_WAITGRANT,
				   timeout);
}

static inline
int monitor_wait_drain(struct syncobj *sobj, const struct timespec *timeout)
{
	return cobalt_monitor_wait(&sobj->core.monitor,
				   COBALT_MONITOR_WAITDRAIN,
				   timeout);
}

static inline
void monitor_grant(struct syncobj *sobj, struct threadobj *thobj)
{
	cobalt_monitor_grant(&sobj->core.monitor, thobj->core.u_mode);
}

static inline
void monitor_drain(struct syncobj *sobj)
{
	cobalt_monitor_drain(&sobj->core.monitor);
}

static inline
void monitor_drain_all(struct syncobj *sobj)
{
	cobalt_monitor_drain_all(&sobj->core.monitor);
}

static inline void syncobj_init_corespec(struct syncobj *sobj)
{
	int flags = monitor_scope_attribute;
	assert(cobalt_monitor_init(&sobj->core.monitor, flags) == 0);
}

static inline void syncobj_cleanup_corespec(struct syncobj *sobj)
{
	assert(cobalt_monitor_destroy(&sobj->core.monitor) == 0);
}

#else /* CONFIG_XENO_MERCURY */

static inline
int monitor_enter(struct syncobj *sobj)
{
	return -pthread_mutex_lock(&sobj->core.lock);
}

static inline
void monitor_exit(struct syncobj *sobj)
{
	int ret;
	ret = pthread_mutex_unlock(&sobj->core.lock);
	assert(ret == 0);
}

static inline
int monitor_wait_grant(struct syncobj *sobj,
		       struct threadobj *current,
		       const struct timespec *timeout)
{
	if (timeout)
		return -pthread_cond_timedwait(&current->core.grant_sync,
					       &sobj->core.lock, timeout);

	return -pthread_cond_wait(&current->core.grant_sync, &sobj->core.lock);
}

static inline
int monitor_wait_drain(struct syncobj *sobj, const struct timespec *timeout)
{
	if (timeout)
		return -pthread_cond_timedwait(&sobj->core.drain_sync,
					       &sobj->core.lock,
					       timeout);

	return -pthread_cond_wait(&sobj->core.drain_sync, &sobj->core.lock);
}

static inline
void monitor_grant(struct syncobj *sobj, struct threadobj *thobj)
{
	pthread_cond_signal(&thobj->core.grant_sync);
}

static inline
void monitor_drain(struct syncobj *sobj)
{
	pthread_cond_signal(&sobj->core.drain_sync);
}

static inline
void monitor_drain_all(struct syncobj *sobj)
{
	pthread_cond_broadcast(&sobj->core.drain_sync);
}

/*
 * Over Mercury, we implement a complex monitor via a mutex and a
 * couple of condvars, one in the syncobj and the other owned by the
 * thread object.
 */
static inline void syncobj_init_corespec(struct syncobj *sobj)
{
	pthread_mutexattr_t mattr;
	pthread_condattr_t cattr;

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	assert(pthread_mutexattr_setpshared(&mattr, mutex_scope_attribute) == 0);
	pthread_mutex_init(&sobj->core.lock, &mattr);
	pthread_mutexattr_destroy(&mattr);

	pthread_condattr_init(&cattr);
	pthread_condattr_setpshared(&cattr, mutex_scope_attribute);
	pthread_condattr_setclock(&cattr, CLOCK_COPPERPLATE);
	pthread_cond_init(&sobj->core.drain_sync, &cattr);
	pthread_condattr_destroy(&cattr);
}

static inline void syncobj_cleanup_corespec(struct syncobj *sobj)
{
	pthread_cond_destroy(&sobj->core.drain_sync);
	pthread_mutex_destroy(&sobj->core.lock);
}

#endif	/* CONFIG_XENO_MERCURY */

void syncobj_init(struct syncobj *sobj, int flags,
		  fnref_type(void (*)(struct syncobj *sobj)) finalizer)
{
	sobj->flags = flags;
	list_init(&sobj->pend_list);
	list_init(&sobj->drain_list);
	sobj->pend_count = 0;
	sobj->drain_count = 0;
	sobj->release_count = 0;
	sobj->finalizer = finalizer;
	syncobj_init_corespec(sobj);
}

int syncobj_lock(struct syncobj *sobj, struct syncstate *syns)
{
	int ret, oldstate;

	assert(threadobj_current() != NULL);

	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);

	ret = monitor_enter(sobj);
	if (ret) {
		pthread_setcancelstate(oldstate, NULL);
		return ret;
	}

	syns->state = oldstate;

	return 0;
}

void syncobj_unlock(struct syncobj *sobj, struct syncstate *syns)
{
	monitor_exit(sobj);
	pthread_setcancelstate(syns->state, NULL);
}

static void syncobj_test_finalize(struct syncobj *sobj,
				  struct syncstate *syns)
{
	void (*finalizer)(struct syncobj *sobj);
	int relcount;

	relcount = --sobj->release_count;
	monitor_exit(sobj);

	if (relcount == 0) {
		syncobj_cleanup_corespec(sobj);
		fnref_get(finalizer, sobj->finalizer);
		if (finalizer)
			finalizer(sobj);
	} else
		assert(relcount > 0);

	/*
	 * Cancelability reset is postponed until here, so that we
	 * can't be wiped off asynchronously before the object is
	 * fully finalized, albeit we exited the monitor earlier to
	 * allow deletion.
	 */
	pthread_setcancelstate(syns->state, NULL);
}

int __syncobj_signal_drain(struct syncobj *sobj)
{
	/* Release one thread waiting for the object to drain. */
	--sobj->drain_count;
	monitor_drain(sobj);

	return 1;
}

/*
 * NOTE: we don't use POSIX cleanup handlers in syncobj_pend() and
 * syncobj_wait() on purpose: these may have a significant impact on
 * latency due to I-cache misses on low-end hardware (e.g. ~6 us on
 * MPC5200), particularly when unwinding the cancel frame. So the
 * cleanup handler below is called by the threadobj finalizer instead
 * when appropriate, since we have enough internal information to
 * handle this situation.
 */
void __syncobj_cleanup_wait(struct syncobj *sobj, struct threadobj *thobj)
{
	/*
	 * We don't care about resetting the original cancel type
	 * saved in the syncstate struct since we are there precisely
	 * because the caller got cancelled.
	 */
	list_remove(&thobj->wait_link);
	if (thobj->wait_status & SYNCOBJ_DRAINING)
		sobj->drain_count--;

	monitor_exit(sobj);
}

static inline void enqueue_waiter(struct syncobj *sobj,
				  struct threadobj *thobj)
{
	struct threadobj *__thobj;

	thobj->wait_prio = threadobj_get_priority(thobj);
	sobj->pend_count++;
	if ((sobj->flags & SYNCOBJ_PRIO) == 0 || list_empty(&sobj->pend_list)) {
		list_append(&thobj->wait_link, &sobj->pend_list);
		return;
	}

	list_for_each_entry_reverse(__thobj, &sobj->pend_list, wait_link) {
		if (thobj->wait_prio <= __thobj->wait_prio)
			break;
	}
	ath(&__thobj->wait_link, &thobj->wait_link);
}

int syncobj_pend(struct syncobj *sobj, const struct timespec *timeout,
		 struct syncstate *syns)
{
	struct threadobj *current = threadobj_current();
	int ret, state;

	assert(current != NULL);

	current->wait_status = 0;
	enqueue_waiter(sobj, current);
	current->wait_sobj = sobj;

	if (current->wait_hook)
		current->wait_hook(sobj, SYNCOBJ_BLOCK);

	/*
	 * XXX: we are guaranteed to be in deferred cancel mode, with
	 * cancelability disabled (in syncobj_lock); enable
	 * cancelability before pending on the condvar.
	 */
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &state);
	/*
	 * Catch spurious unlocked calls: this must be a blatant bug
	 * in the calling code, don't even try to continue
	 * (syncobj_lock() required first).
	 */
	assert(state == PTHREAD_CANCEL_DISABLE);

	do {
		ret = monitor_wait_grant(sobj, current, timeout);
		/* Check for spurious wake up. */
	} while (ret == 0 && current->wait_sobj);

	pthread_setcancelstate(state, NULL);

	if (current->wait_hook)
		current->wait_hook(sobj, SYNCOBJ_RESUME);

	if (ret) {
		current->wait_sobj = NULL;
		list_remove(&current->wait_link);
	} else if (current->wait_status & SYNCOBJ_DELETED) {
		syncobj_test_finalize(sobj, syns);
		ret = -EIDRM;
	} else if (current->wait_status & SYNCOBJ_RELEASE_MASK) {
		--sobj->release_count;
		assert(sobj->release_count >= 0);
		if (current->wait_status & SYNCOBJ_FLUSHED)
			ret = -EINTR;
	}

	return ret;
}

void syncobj_requeue_waiter(struct syncobj *sobj, struct threadobj *thobj)
{
	list_remove(&thobj->wait_link);
	enqueue_waiter(sobj, thobj);
}

void syncobj_wakeup_waiter(struct syncobj *sobj, struct threadobj *thobj)
{
	list_remove(&thobj->wait_link);
	thobj->wait_sobj = NULL;
	sobj->pend_count--;
	monitor_grant(sobj, thobj);
}

struct threadobj *syncobj_post(struct syncobj *sobj)
{
	struct threadobj *thobj;

	if (list_empty(&sobj->pend_list))
		return NULL;

	thobj = list_pop_entry(&sobj->pend_list, struct threadobj, wait_link);
	thobj->wait_sobj = NULL;
	sobj->pend_count--;
	monitor_grant(sobj, thobj);

	return thobj;
}

struct threadobj *syncobj_peek_at_pend(struct syncobj *sobj)
{
	struct threadobj *thobj;

	if (list_empty(&sobj->pend_list))
		return NULL;

	thobj = list_first_entry(&sobj->pend_list, struct threadobj,
				 wait_link);
	return thobj;
}

struct threadobj *syncobj_peek_at_drain(struct syncobj *sobj)
{
	struct threadobj *thobj;

	if (list_empty(&sobj->drain_list))
		return NULL;

	thobj = list_first_entry(&sobj->drain_list, struct threadobj,
				 wait_link);
	return thobj;
}

int syncobj_wait_drain(struct syncobj *sobj, const struct timespec *timeout,
		       struct syncstate *syns)
{
	struct threadobj *current = threadobj_current();
	int ret, state;

	assert(current != NULL);

	/*
	 * XXX: syncobj_wait_drain() behaves slightly differently than
	 * syncobj_pend(), in that we don't process spurious wakeups
	 * internally, leaving it to the caller. We do this because a
	 * drain sync is broadcast so we can't be 100% sure whether
	 * the wait condition actually disappeared for all waiters.
	 *
	 * (e.g. in case the drain signal notifies about a single
	 * resource being released, only one waiter will be satisfied,
	 * albeit all waiters will compete to get that resource - this
	 * means that all waiters but one will get a spurious wakeup).
	 *
	 * On the other hand, syncobj_pend() only unblocks on a
	 * directed wakeup signal to the waiting thread, so we can
	 * check whether such signal has existed prior to exiting the
	 * wait loop (i.e. testing current->wait_sobj for NULL).
	 */
	current->wait_status = SYNCOBJ_DRAINING;
	list_append(&current->wait_link, &sobj->drain_list);
	current->wait_sobj = sobj;
	sobj->drain_count++;

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &state);
	assert(state == PTHREAD_CANCEL_DISABLE);

	if (current->wait_hook)
		current->wait_hook(sobj, SYNCOBJ_BLOCK);

	/*
	 * XXX: The caller must check for spurious wakeups, in case
	 * the drain condition became false again before it resumes.
	 */
	ret = monitor_wait_drain(sobj, timeout);

	pthread_setcancelstate(state, NULL);

	current->wait_status &= ~SYNCOBJ_DRAINING;
	if (current->wait_status == 0) { /* not flushed? */
		current->wait_sobj = NULL;
		list_remove(&current->wait_link);
	}

	if (current->wait_hook)
		current->wait_hook(sobj, SYNCOBJ_RESUME);

	if (current->wait_status & SYNCOBJ_DELETED) {
		syncobj_test_finalize(sobj, syns);
		ret = -EIDRM;
	} else if (current->wait_status & SYNCOBJ_RELEASE_MASK) {
		--sobj->release_count;
		assert(sobj->release_count >= 0);
		if (current->wait_status & SYNCOBJ_FLUSHED)
			ret = -EINTR;
	}

	return ret;
}

int syncobj_flush(struct syncobj *sobj, int reason)
{
	struct threadobj *thobj;

	/* Must have a valid release flag set. */
	assert(reason & SYNCOBJ_RELEASE_MASK);

	while (!list_empty(&sobj->pend_list)) {
		thobj = list_pop_entry(&sobj->pend_list,
				       struct threadobj, wait_link);
		thobj->wait_status |= reason;
		thobj->wait_sobj = NULL;
		monitor_grant(sobj, thobj);
		sobj->release_count++;
	}
	sobj->pend_count = 0;

	if (sobj->drain_count > 0) {
		do {
			thobj = list_pop_entry(&sobj->drain_list,
					       struct threadobj, wait_link);
			thobj->wait_sobj = NULL;
			thobj->wait_status |= reason;
		} while (!list_empty(&sobj->drain_list));
		sobj->release_count += sobj->drain_count;
		sobj->drain_count = 0;
		monitor_drain_all(sobj);
	}

	return sobj->release_count;
}

int syncobj_destroy(struct syncobj *sobj, struct syncstate *syns)
{
	int ret;

	ret = syncobj_flush(sobj, SYNCOBJ_DELETED);
	if (ret == 0) {
		/* No thread awaken - we may dispose immediately. */
		sobj->release_count = 1;
		syncobj_test_finalize(sobj, syns);
	} else
		syncobj_unlock(sobj, syns);

	return ret;
}

void syncobj_uninit(struct syncobj *sobj)
{
	assert(sobj->release_count == 0);
	syncobj_cleanup_corespec(sobj);
}

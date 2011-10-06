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
 * using an internal queue.
 *
 * We also use this queue to implement the flush operation on
 * synchronization objects which POSIX does not provide either. Atomic
 * release is emulated by the sobj->lock mutex acting as a barrier for
 * all waiters, after their condition variable is signaled by the
 * flushing code, and until the latter releases this lock. We rely on
 * the scheduling priority as enforced by the kernel to fix the
 * release order whenever the lock is contended (i.e. we readied more
 * than a single waiter when flushing).
 *
 * NOTE: we do no do error backtracing in this file, since error
 * returns when locking, pending or deleting sync objects express
 * normal runtime conditions.
 */

void syncobj_init(struct syncobj *sobj, int flags,
		  fnref_type(void (*)(struct syncobj *sobj)) finalizer)
{
	pthread_mutexattr_t mattr;
	pthread_condattr_t cattr;

	sobj->flags = flags;
	list_init(&sobj->pend_list);
	list_init(&sobj->drain_list);
	sobj->pend_count = 0;
	sobj->drain_count = 0;
	sobj->release_count = 0;
	sobj->finalizer = finalizer;

	__RT(pthread_mutexattr_init(&mattr));
	__RT(pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT));
	assert(__RT(pthread_mutexattr_setpshared(&mattr, mutex_scope_attribute)) == 0);
	__RT(pthread_mutex_init(&sobj->lock, &mattr));
	__RT(pthread_mutexattr_destroy(&mattr));

	__RT(pthread_condattr_init(&cattr));
	__RT(pthread_condattr_setpshared(&cattr, mutex_scope_attribute));
	__RT(pthread_condattr_setclock(&cattr, CLOCK_COPPERPLATE));
	__RT(pthread_cond_init(&sobj->post_sync, &cattr));
	__RT(pthread_condattr_destroy(&cattr));
}

static void syncobj_test_finalize(struct syncobj *sobj,
				  struct syncstate *syns)
{
	void (*finalizer)(struct syncobj *sobj);
	int relcount;

	relcount = --sobj->release_count;
	__RT(pthread_mutex_unlock(&sobj->lock));

	if (relcount == 0) {
		__RT(pthread_cond_destroy(&sobj->post_sync));
		__RT(pthread_mutex_destroy(&sobj->lock));
		fnref_get(finalizer, sobj->finalizer);
		if (finalizer)
			finalizer(sobj);
	} else
		assert(relcount > 0);

	/*
	 * Cancelability reset is postponed until here, so that we
	 * can't be wiped off before the object is fully finalized,
	 * albeit we did unlock the object earlier to allow
	 * deletion. This is why we don't use the all-in-one
	 * write_unlock_safe() call.
	 */
	pthread_setcancelstate(syns->state, NULL);
}

static void enqueue_waiter(struct syncobj *sobj,
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

int __syncobj_signal_drain(struct syncobj *sobj)
{
	/* Release one thread waiting for the object to drain. */
	--sobj->drain_count;
	__RT(pthread_cond_signal(&sobj->post_sync));

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
void __syncobj_cleanup_wait(struct syncobj *sobj,
			    struct threadobj *thobj)
{
	/*
	 * We don't care about resetting the original cancel type
	 * saved in the syncstate struct since we are there precisely
	 * because the caller got cancelled.
	 */
	if (holder_linked(&thobj->wait_link)) {
		list_remove(&thobj->wait_link);
		if (thobj->wait_status & SYNCOBJ_DRAINING)
			sobj->drain_count--;
	}
	__RT(pthread_mutex_unlock(&sobj->lock));
}

int syncobj_pend(struct syncobj *sobj, struct timespec *timeout,
		 struct syncstate *syns)
{
	struct threadobj *current = threadobj_current();
	int ret, state;

	assert(current != NULL);

	current->wait_sobj = sobj;
	current->wait_status = 0;
	enqueue_waiter(sobj, current);

	if (current->wait_hook)
		current->wait_hook(current, SYNCOBJ_BLOCK);

	/*
	 * XXX: we are guaranteed to be in deferred cancel mode, with
	 * cancelability disabled (in syncobj_lock); enable
	 * cancelability before pending on the condvar.
	 */
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &state);

	do {
		if (timeout)
			ret = __RT(pthread_cond_timedwait(&current->wait_sync,
							  &sobj->lock, timeout));
		else
			ret = __RT(pthread_cond_wait(&current->wait_sync,
						     &sobj->lock));
		/* Check for spurious wake up. */
	} while (ret == 0 && holder_linked(&current->wait_link));

	pthread_setcancelstate(state, NULL);

	if (current->wait_hook)
		current->wait_hook(current, SYNCOBJ_RESUME);

	current->wait_sobj = NULL;

	if (ret)
		list_remove_init(&current->wait_link);
	else if (current->wait_status & SYNCOBJ_DELETED) {
		syncobj_test_finalize(sobj, syns);
		ret = EIDRM;
	} else if (current->wait_status & SYNCOBJ_RELEASE_MASK) {
		--sobj->release_count;
		assert(sobj->release_count >= 0);
		if (current->wait_status & SYNCOBJ_FLUSHED)
			ret = EINTR;
	}

	return -ret;
}

void syncobj_requeue_waiter(struct syncobj *sobj, struct threadobj *thobj)
{
	list_remove_init(&thobj->wait_link);
	enqueue_waiter(sobj, thobj);
}

void syncobj_wakeup_waiter(struct syncobj *sobj, struct threadobj *thobj)
{
	list_remove_init(&thobj->wait_link);
	sobj->pend_count--;
	__RT(pthread_cond_signal(&thobj->wait_sync));
}

struct threadobj *syncobj_post(struct syncobj *sobj)
{
	struct threadobj *thobj;

	if (list_empty(&sobj->pend_list))
		return NULL;

	thobj = list_pop_entry(&sobj->pend_list, struct threadobj, wait_link);
	sobj->pend_count--;
	__RT(pthread_cond_signal(&thobj->wait_sync));

	return thobj;
}

struct threadobj *syncobj_peek(struct syncobj *sobj)
{
	struct threadobj *thobj;

	if (list_empty(&sobj->pend_list))
		return NULL;

	thobj = list_first_entry(&sobj->pend_list, struct threadobj,
				 wait_link);
	return thobj;
}

int syncobj_wait_drain(struct syncobj *sobj, struct timespec *timeout,
		       struct syncstate *syns)
{
	struct threadobj *current = threadobj_current();
	int ret, state;

	/*
	 * XXX: The caller must check for spurious wakeups, in case
	 * the drain condition became false again before it resumes.
	 */
	current->wait_sobj = sobj;
	current->wait_status = SYNCOBJ_DRAINING;
	list_append(&current->wait_link, &sobj->drain_list);
	sobj->drain_count++;

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &state);

	if (current->wait_hook)
		current->wait_hook(current, SYNCOBJ_BLOCK);

	if (timeout)
		ret = __RT(pthread_cond_timedwait(&sobj->post_sync,
						  &sobj->lock, timeout));
	else
		ret = __RT(pthread_cond_wait(&sobj->post_sync, &sobj->lock));

	pthread_setcancelstate(state, NULL);

	current->wait_status &= ~SYNCOBJ_DRAINING;
	if (current->wait_status == 0)
		list_remove_init(&current->wait_link);

	if (current->wait_hook)
		current->wait_hook(current, SYNCOBJ_RESUME);

	current->wait_sobj = NULL;

	if (current->wait_status & SYNCOBJ_DELETED) {
		syncobj_test_finalize(sobj, syns);
		ret = EIDRM;
	} else if (current->wait_status & SYNCOBJ_RELEASE_MASK) {
		--sobj->release_count;
		assert(sobj->release_count >= 0);
		if (current->wait_status & SYNCOBJ_FLUSHED)
			ret = EINTR;
	}

	return -ret;
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
		__RT(pthread_cond_signal(&thobj->wait_sync));
		sobj->release_count++;
	}
	sobj->pend_count = 0;

	if (sobj->drain_count > 0) {
		do {
			thobj = list_pop_entry(&sobj->drain_list,
					       struct threadobj, wait_link);
			thobj->wait_status |= reason;
		} while (!list_empty(&sobj->drain_list));
		sobj->release_count += sobj->drain_count;
		sobj->drain_count = 0;
		__RT(pthread_cond_broadcast(&sobj->post_sync));
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

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
 */

void syncobj_init(struct syncobj *sobj, int flags,
		  fnref_type(void (*)(struct syncobj *sobj)) finalizer)
{
	pthread_mutexattr_t mattr;
	pthread_condattr_t cattr;

	sobj->flags = flags;
	list_init(&sobj->pend_list);
	list_init(&sobj->drain_list);
	sobj->drain_count = 0;
	sobj->release_count = 0;
	sobj->finalizer = finalizer;
	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	assert(pthread_mutexattr_setpshared(&mattr, mutex_scope_attribute) == 0);
	pthread_mutex_init(&sobj->lock, &mattr);
	pthread_mutexattr_destroy(&mattr);
	pthread_condattr_init(&cattr);
	pthread_condattr_setpshared(&cattr, mutex_scope_attribute);
	pthread_cond_init(&sobj->post_sync, &cattr);
	pthread_condattr_destroy(&cattr);
}

int syncobj_lock(struct syncobj *sobj)
{
	int ret, otype;

	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &otype);
	ret = -pthread_mutex_lock(&sobj->lock);
	if (ret)
		pthread_setcanceltype(otype, NULL);
	else
		sobj->cancel_type = otype;

	return ret;
}

void syncobj_unlock(struct syncobj *sobj)
{
	int otype = sobj->cancel_type;
	pthread_mutex_unlock(&sobj->lock);
	pthread_setcanceltype(otype, NULL);
}

static void syncobj_test_finalize(struct syncobj *sobj)
{
	void (*finalizer)(struct syncobj *sobj);
	int relcount;

	relcount = --sobj->release_count;
	pthread_mutex_unlock(&sobj->lock);

	if (relcount == 0) {
		pthread_cond_destroy(&sobj->post_sync);
		pthread_mutex_destroy(&sobj->lock);
		fnref_get(finalizer, sobj->finalizer);
		if (finalizer)
			finalizer(sobj);
	} else
		assert(relcount > 0);
}

static void syncobj_cleanup_wait(void *arg)
{
	struct threadobj *current = threadobj_current();
	struct syncobj *sobj = arg;

	list_remove_init(&current->wait_link);
	pthread_mutex_unlock(&sobj->lock);
}

static void syncobj_enqueue_waiter(struct syncobj *sobj, struct threadobj *thobj)
{
	struct threadobj *__thobj;

	if (sobj->flags & SYNCOBJ_PRIO) {
		thobj->wait_prio = threadobj_get_priority(thobj);
		list_for_each_entry_reverse(__thobj, &sobj->pend_list, wait_link) {
			if (thobj->wait_prio <= __thobj->wait_prio)
				break;
		}
		ath(&__thobj->wait_link, &thobj->wait_link);
	} else
		list_append(&thobj->wait_link, &sobj->pend_list);
}

int __syncobj_signal_drain(struct syncobj *sobj)
{
	/* Release one thread waiting for the object to drain. */
	--sobj->drain_count;
	pthread_cond_signal(&sobj->post_sync);

	return 1;
}

int syncobj_pend(struct syncobj *sobj, struct timespec *timeout)
{
	struct threadobj *current = threadobj_current();
	int ret, otype;

	assert(current != NULL);

	pthread_cleanup_push(syncobj_cleanup_wait, sobj);

	syncobj_enqueue_waiter(sobj, current);
	current->wait_sobj = sobj;
	current->wait_status = 0;

	if (current->wait_hook)
		current->wait_hook(current, SYNCOBJ_BLOCK);

	do {
		if (timeout)
			ret = pthread_cond_timedwait(&current->wait_sync,
						     &sobj->lock, timeout);
		else
			ret = pthread_cond_wait(&current->wait_sync,
						&sobj->lock);
		/* Check for spurious wake up. */
	} while (ret == 0 && holder_linked(&current->wait_link));

	if (current->wait_hook)
		current->wait_hook(current, SYNCOBJ_RESUME);

	current->wait_sobj = NULL;

	if (ret)
		list_remove_init(&current->wait_link);
	else if (current->wait_status & SYNCOBJ_FLUSHED) {
		--sobj->release_count;
		assert(sobj->release_count >= 0);
		ret = EINTR;
	} else if (current->wait_status & SYNCOBJ_DELETED) {
		otype = sobj->cancel_type;
		syncobj_test_finalize(sobj);
		pthread_setcanceltype(otype, NULL);
		ret = EIDRM;
	}

	pthread_cleanup_pop(0);

	return -ret;
}

void syncobj_requeue_waiter(struct syncobj *sobj, struct threadobj *thobj)
{
	list_remove_init(&thobj->wait_link);
	syncobj_enqueue_waiter(sobj, thobj);
}

void syncobj_wakeup_waiter(struct syncobj *sobj, struct threadobj *thobj)
{
	list_remove_init(&thobj->wait_link);
	pthread_cond_signal(&thobj->wait_sync);
}

struct threadobj *syncobj_post(struct syncobj *sobj)
{
	struct threadobj *thobj;

	if (list_empty(&sobj->pend_list))
		return NULL;

	thobj = list_pop_entry(&sobj->pend_list, struct threadobj, wait_link);
	pthread_cond_signal(&thobj->wait_sync);

	return thobj;
}

int syncobj_wait_drain(struct syncobj *sobj, struct timespec *timeout)
{
	struct threadobj *current = threadobj_current();
	int ret, otype;

	/*
	 * XXX: The caller must check for spurious wakeups, in case
	 * the drain condition became false again before it resumes.
	 */
	pthread_cleanup_push(syncobj_cleanup_wait, sobj);

	list_append(&current->wait_link, &sobj->drain_list);
	sobj->drain_count++;
	current->wait_sobj = sobj;
	current->wait_status = 0;

	if (current->wait_hook)
		current->wait_hook(current, SYNCOBJ_BLOCK);

	if (timeout)
		ret = pthread_cond_timedwait(&sobj->post_sync,
					     &sobj->lock, timeout);
	else
		ret = pthread_cond_wait(&sobj->post_sync, &sobj->lock);

	if (current->wait_status == 0)
		list_remove_init(&current->wait_link);

	if (current->wait_hook)
		current->wait_hook(current, SYNCOBJ_RESUME);

	current->wait_sobj = NULL;

	pthread_cleanup_pop(0);

	if (current->wait_status & SYNCOBJ_FLUSHED) {
		--sobj->release_count;
		assert(sobj->release_count >= 0);
		ret = EINTR;
	} else if (current->wait_status & SYNCOBJ_DELETED) {
		otype = sobj->cancel_type;
		syncobj_test_finalize(sobj);
		pthread_setcanceltype(otype, NULL);
		ret = EIDRM;
	}

	return -ret;
}

int syncobj_flush(struct syncobj *sobj, int reason)
{
	struct threadobj *thobj;

	while (!list_empty(&sobj->pend_list)) {
		thobj = list_pop_entry(&sobj->pend_list,
				       struct threadobj, wait_link);
		thobj->wait_status |= reason;
		pthread_cond_signal(&thobj->wait_sync);
		sobj->release_count++;
	}

	if (sobj->drain_count > 0) {
		do {
			thobj = list_pop_entry(&sobj->drain_list,
					       struct threadobj, wait_link);
			thobj->wait_status |= reason;
		} while (!list_empty(&sobj->drain_list));
		sobj->release_count += sobj->drain_count;
		sobj->drain_count = 0;
		pthread_cond_broadcast(&sobj->post_sync);
	}

	return sobj->release_count;
}

int syncobj_destroy(struct syncobj *sobj)
{
	int ret, otype;

	ret = syncobj_flush(sobj, SYNCOBJ_DELETED);
	if (ret == 0) {
		/* No thread awaken - we may dispose immediately. */
		sobj->release_count = 1;
		otype = sobj->cancel_type;
		syncobj_test_finalize(sobj);
		pthread_setcanceltype(otype, NULL);
	} else
		syncobj_unlock(sobj);

	return ret;
}

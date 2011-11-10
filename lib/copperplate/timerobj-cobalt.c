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
 * Timer object abstraction - Cobalt core version.
 */

#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <memory.h>
#include <pthread.h>
#include <semaphore.h>
#include "copperplate/list.h"
#include "copperplate/lock.h"
#include "copperplate/threadobj.h"
#include "copperplate/timerobj.h"
#include "copperplate/clockobj.h"
#include "copperplate/debug.h"
#include "internal.h"

static sem_t svsem;

static pthread_mutex_t svlock;

static pthread_t svthread;

static DEFINE_PRIVATE_LIST(svtimers);

void timespec_add(struct timespec *r,
		  const struct timespec *t1, const struct timespec *t2);

static inline int __attribute__ ((always_inline))
timeobj_compare(const struct timespec *t1, const struct timespec *t2)
{
	if (t1->tv_sec < t2->tv_sec)
		return -1;
	if (t1->tv_sec > t2->tv_sec)
		return 1;
	if (t1->tv_nsec < t2->tv_nsec)
		return -1;
	if (t1->tv_nsec > t2->tv_nsec)
		return 1;

	return 0;
}

/*
 * XXX: at some point, we may consider using a timer wheel instead of
 * a simple linked list to index timers. The latter method is
 * efficient for up to ten outstanding timers or so, which should be
 * enough for most applications. However, there exist poorly designed
 * apps involving dozens of active timers, particularly in the legacy
 * embedded world.
 */
static void timerobj_enqueue(struct timerobj *tmobj)
{
	struct timerobj *__tmobj;

	if (pvlist_empty(&svtimers)) {
		pvlist_append(&tmobj->link, &svtimers);
		return;
	}

	pvlist_for_each_entry_reverse(__tmobj, &svtimers, link) {
		if (timeobj_compare(&__tmobj->spec.it_value,
				    &tmobj->spec.it_value) <= 0)
			break;
	}

	atpvh(&__tmobj->link, &tmobj->link);
}

static void *timerobj_server(void *arg)
{
	struct timespec now, value, interval;
	struct timerobj *tmobj, *tmp;
	int ret;

	pthread_set_name_np(pthread_self(), "timer-internal");
	pthread_setspecific(threadobj_tskey, THREADOBJ_IRQCONTEXT);

	for (;;) {
		ret = __RT(sem_wait(&svsem));
		if (ret && ret != EINTR)
			break;

		/*
		 * We have a single server thread for now, so handlers
		 * are fully serialized.
		 */
		push_cleanup_lock(&svlock);
		write_lock(&svlock);

		__RT(clock_gettime(CLOCK_COPPERPLATE, &now));

		pvlist_for_each_entry_safe(tmobj, tmp, &svtimers, link) {
			value = tmobj->spec.it_value;
			if (timeobj_compare(&value, &now) > 0)
				break;
			pvlist_remove_init(&tmobj->link);
			interval = tmobj->spec.it_interval;
			if (interval.tv_sec > 0 || interval.tv_nsec > 0) {
				timespec_add(&tmobj->spec.it_value,
					     &value, &interval);
				timerobj_enqueue(tmobj);
			}
			write_unlock(&svlock);
			tmobj->handler(tmobj);
			write_lock(&svlock);
		}

		write_unlock(&svlock);
		pop_cleanup_lock(&svlock);
	}

	return NULL;
}

static int timerobj_spawn_server(void)
{
	int ret = 0;

	push_cleanup_lock(&svlock);
	read_lock(&svlock);

	if (svthread)
		goto out;

	ret = __bt(copperplate_create_thread(threadobj_irq_prio,
					     timerobj_server, NULL,
					     PTHREAD_STACK_MIN * 16,
					     &svthread));
out:
	read_unlock(&svlock);
	pop_cleanup_lock(&svlock);

	return ret;
}

int timerobj_init(struct timerobj *tmobj)
{
	pthread_mutexattr_t mattr;
	struct sigevent evt;
	int ret;

	/*
	 * XXX: We need a threaded handler so that we may invoke core
	 * async-unsafe services from there (e.g. syncobj post
	 * routines are not async-safe, but the higher layers may
	 * invoke them from a timer handler).
	 */
	ret = timerobj_spawn_server();
	if (ret)
		return __bt(ret);

	evt.sigev_notify = SIGEV_THREAD_ID;
	evt.sigev_value.sival_ptr = &svsem;

	tmobj->handler = NULL;
	pvholder_init(&tmobj->link); /* so we may use pvholder_linked() */

	if (__RT(timer_create(CLOCK_COPPERPLATE, &evt, &tmobj->timer)))
		return __bt(-errno);

	__RT(pthread_mutexattr_init(&mattr));
	__RT(pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT));
	assert(__RT(pthread_mutexattr_setpshared(&mattr, mutex_scope_attribute)) == 0);
	__RT(pthread_mutex_init(&tmobj->lock, &mattr));
	__RT(pthread_mutexattr_destroy(&mattr));

	return 0;
}

void timerobj_destroy(struct timerobj *tmobj) /* lock held, dropped */
{
	write_lock_nocancel(&svlock);

	if (pvholder_linked(&tmobj->link))
		pvlist_remove_init(&tmobj->link);

	write_unlock(&svlock);

	__RT(timer_delete(tmobj->timer));
	__RT(pthread_mutex_unlock(&tmobj->lock));
	__RT(pthread_mutex_destroy(&tmobj->lock));
}

int timerobj_start(struct timerobj *tmobj,
		   void (*handler)(struct timerobj *tmobj),
		   struct itimerspec *it) /* lock held, dropped */
{
	tmobj->handler = handler;
	tmobj->spec = *it;
	write_lock_nocancel(&svlock);
	timerobj_enqueue(tmobj);
	write_unlock(&svlock);
	timerobj_unlock(tmobj);

	if (__RT(timer_settime(tmobj->timer, TIMER_ABSTIME, it, NULL)))
		return __bt(-errno);

	return 0;
}

static const struct itimerspec itimer_stop;

int timerobj_stop(struct timerobj *tmobj) /* lock held, dropped */
{
	write_lock_nocancel(&svlock);

	if (pvholder_linked(&tmobj->link))
		pvlist_remove_init(&tmobj->link);

	write_unlock(&svlock);

	__RT(timer_settime(tmobj->timer, 0, &itimer_stop, NULL));
	tmobj->handler = NULL;
	timerobj_unlock(tmobj);

	return 0;
}

int timerobj_pkg_init(void)
{
	pthread_mutexattr_t mattr;
	int ret;

	ret = __RT(sem_init(&svsem, 0, 0));
	if (ret)
		return __bt(-errno);

	__RT(pthread_mutexattr_init(&mattr));
	__RT(pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT));
	__RT(pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE));
	__RT(pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_PRIVATE));
	ret = __RT(pthread_mutex_init(&svlock, &mattr));
	__RT(pthread_mutexattr_destroy(&mattr));
	if (ret)
		return __bt(-ret);

	return 0;
}

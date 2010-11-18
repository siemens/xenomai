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

#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <memory.h>
#include <pthread.h>
#include <semaphore.h>
#include "copperplate/list.h"
#include "copperplate/threadobj.h"
#include "copperplate/timerobj.h"
#include "copperplate/timerobj.h"

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

	for (;;) {
		ret = sem_wait(&svsem);
		if (ret && ret != EINTR)
			break;

		/* Caller may assume that handlers are serialized. */
		pthread_mutex_lock(&svlock);

		clock_gettime(CLOCK_REALTIME, &now);

		pvlist_for_each_entry_safe(tmobj, tmp, &svtimers, link) {
			value = tmobj->spec.it_value;
			if (timeobj_compare(&value, &now) > 0)
				break;
			pvlist_remove(&tmobj->link);
			interval = tmobj->spec.it_interval;
			if (interval.tv_sec > 0 || interval.tv_nsec > 0) {
				timespec_add(&tmobj->spec.it_value,
					     &value, &interval);
				timerobj_enqueue(tmobj);
			}
			threadobj_ienter();
			tmobj->handler(tmobj);
			threadobj_iexit();
		}

		pthread_mutex_unlock(&svlock);
	}

	return NULL;
}

static int timerobj_spawn_server(void)
{
	struct sched_param param;
	pthread_attr_t thattr;
	int ret = 0;

	pthread_mutex_lock(&svlock);

	if (svthread)
		goto out;

	pthread_attr_init(&thattr);
	pthread_attr_setschedpolicy(&thattr, SCHED_FIFO);
	memset(&param, 0, sizeof(param));
	param.sched_priority = threadobj_irq_priority();
	pthread_attr_setschedparam(&thattr, &param);
	ret = pthread_create(&svthread, &thattr, timerobj_server, NULL);
out:
	pthread_mutex_unlock(&svlock);

	return ret;
}

int timerobj_init(struct timerobj *tmobj)
{
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
		return ret;

	evt.sigev_notify = SIGEV_THREAD_ID;
	evt.sigev_value.sival_ptr = &svsem;

	tmobj->handler = NULL;
	pvholder_init(&tmobj->link); /* so we may use pvholder_linked() */

	if (timer_create(CLOCK_REALTIME, &evt, &tmobj->timer))
		return -errno;

	return 0;
}

int timerobj_destroy(struct timerobj *tmobj)
{
	pthread_mutex_lock(&svlock);

	if (pvholder_linked(&tmobj->link))
		pvlist_remove(&tmobj->link);

	pthread_mutex_unlock(&svlock);

	if (timer_delete(tmobj->timer))
		return -errno;

	return 0;
}

int timerobj_start(struct timerobj *tmobj,
		   void (*handler)(struct timerobj *tmobj),
		   struct itimerspec *it)
{
	tmobj->handler = handler;
	tmobj->spec = *it;
	pthread_mutex_lock(&svlock);
	timerobj_enqueue(tmobj);
	pthread_mutex_unlock(&svlock);

	if (timer_settime(tmobj->timer, TIMER_ABSTIME, it, NULL))
		return -errno;

	return 0;
}

static const struct itimerspec itimer_stop = {
	.it_value = {
		.tv_sec = 0,
		.tv_nsec = 0,
	},
	.it_interval =  {
		.tv_sec = 0,
		.tv_nsec = 0,
	},
};

int timerobj_stop(struct timerobj *tmobj)
{
	pthread_mutex_lock(&svlock);

	if (pvholder_linked(&tmobj->link))
		pvlist_remove(&tmobj->link);

	pthread_mutex_unlock(&svlock);

	if (timer_settime(tmobj->timer, 0, &itimer_stop, NULL))
		return -errno;

	tmobj->handler = NULL;

	return 0;
}

int timerobj_pkg_init(void)
{
	pthread_mutexattr_t mattr;
	int ret;

	ret = sem_init(&svsem, 0, 0);
	if (ret)
		return -errno;

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_PRIVATE);
	ret = pthread_mutex_init(&svlock, &mattr);
	pthread_mutexattr_destroy(&mattr);
	if (ret)
		return -ret;

	return 0;
}

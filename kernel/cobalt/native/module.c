/**
 * @file
 * This file is part of the Xenomai project.
 *
 * @note Copyright (C) 2004 Philippe Gerum <rpm@xenomai.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*!
 * \defgroup native Native Xenomai API.
 *
 * The native Xenomai programming interface available to real-time
 * applications. This API is built over the abstract RTOS core
 * implemented by the Xenomai nucleus.
 *
 */

#include <native/task.h>
#include <native/timer.h>
#include <native/sem.h>
#include <native/event.h>
#include <native/mutex.h>
#include <native/cond.h>
#include <native/queue.h>
#include <native/heap.h>
#include <native/alarm.h>
#include <native/syscall.h>

MODULE_DESCRIPTION("Native skin");
MODULE_AUTHOR("rpm@xenomai.org");
MODULE_LICENSE("GPL");

xeno_rholder_t __native_global_rholder;

DEFINE_XNPTREE(__native_ptree, "native");

int SKIN_INIT(native)
{
	int err;

	initq(&__native_global_rholder.alarmq);
	initq(&__native_global_rholder.condq);
	initq(&__native_global_rholder.eventq);
	initq(&__native_global_rholder.heapq);
	initq(&__native_global_rholder.mutexq);
	initq(&__native_global_rholder.queueq);
	initq(&__native_global_rholder.semq);
	initq(&__native_global_rholder.bufferq);

	err = xnpod_init();

	if (err)
		goto fail;

	err = __native_task_pkg_init();

	if (err)
		goto cleanup_pod;

	err = __native_sem_pkg_init();

	if (err)
		goto cleanup_task;

	err = __native_event_pkg_init();

	if (err)
		goto cleanup_sem;

	err = __native_mutex_pkg_init();

	if (err)
		goto cleanup_event;

	err = __native_cond_pkg_init();

	if (err)
		goto cleanup_mutex;

	err = __native_queue_pkg_init();

	if (err)
		goto cleanup_cond;

	err = __native_heap_pkg_init();

	if (err)
		goto cleanup_queue;

	err = __native_alarm_pkg_init();

	if (err)
		goto cleanup_heap;

	err = __native_syscall_init();

	if (err)
		goto cleanup_alarm;

	xnprintf("starting native API services.\n");

	return 0;		/* SUCCESS. */

      cleanup_alarm:

	__native_alarm_pkg_cleanup();

      cleanup_heap:

	__native_heap_pkg_cleanup();

      cleanup_queue:

	__native_queue_pkg_cleanup();

      cleanup_cond:

	__native_cond_pkg_cleanup();

      cleanup_mutex:

	__native_mutex_pkg_cleanup();

      cleanup_event:

	__native_event_pkg_cleanup();

      cleanup_sem:

	__native_sem_pkg_cleanup();

      cleanup_task:

	__native_task_pkg_cleanup();

      cleanup_pod:

	xnpod_shutdown(err);

      fail:

	xnlogerr("native skin init failed, code %d.\n", err);

	return err;
}

void SKIN_EXIT(native)
{
	xnprintf("stopping native API services.\n");

	__native_alarm_pkg_cleanup();
	__native_heap_pkg_cleanup();
	__native_queue_pkg_cleanup();
	__native_cond_pkg_cleanup();
	__native_mutex_pkg_cleanup();
	__native_event_pkg_cleanup();
	__native_sem_pkg_cleanup();
	__native_task_pkg_cleanup();
	__native_syscall_cleanup();

	xnpod_shutdown(XNPOD_NORMAL_EXIT);
}

module_init(__native_skin_init);
module_exit(__native_skin_exit);

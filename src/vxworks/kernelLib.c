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
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <copperplate/init.h>
#include <copperplate/hash.h>
#include <vxworks/errnoLib.h>
#include <vxworks/kernelLib.h>
#include "tickLib.h"
#include "taskLib.h"

STATUS kernelInit(FUNCPTR rootRtn, int argc, char *const argv[])
{
	TASK_ID tid;
	int ret;

	/*
	 * XXX: we don't set any protected section here, since we must
	 * be running over the main thread, so if we get cancelled,
	 * everything goes away anyway.
	 */

	ret = copperplate_init(argc, argv);
	if (ret)
		return ret;

	registry_add_dir("/vxworks");
	registry_add_dir("/vxworks/tasks");
	registry_add_dir("/vxworks/semaphores");
	registry_add_dir("/vxworks/queues");
	registry_add_dir("/vxworks/watchdogs");

	cluster_init(&wind_task_table, "vxworks.task");

	ret = clockobj_init(&wind_clock,
			    "vxworks", __tick_period_arg * 1000);

	if (ret) {
		warning("%s: failed to initialize VxWorks clock (period=%uus)",
			__FUNCTION__, __tick_period_arg);
		errno = -ret;
		return ERROR;
	}

	if (rootRtn == NULL)
		return OK;

	tid = taskSpawn("rootTask",
			50,
			0, 0, rootRtn, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

	return tid == ERROR ? ERROR : OK;
}

STATUS kernelTimeSlice(int ticks)
{
	struct service svc;
	struct timespec ts;
	int ret;

	COPPERPLATE_PROTECT(svc);

	if (ticks) {
		clockobj_ticks_to_timeout(&wind_clock, ticks, &ts);
		ret = threadobj_start_rr(&ts);
		assert(ret == 0);
	} else
		threadobj_stop_rr();

	COPPERPLATE_UNPROTECT(svc);

	return OK;
}
      
const char *kernelVersion(void)
{
	return "Xenomai WIND emulator version 2.0";
}

/*
 * Copyright (C) 2012 Philippe Gerum <rpm@xenomai.org>.
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

#include <vxworks/kernLib.h>
#include <vxworks/errnoLib.h>
#include "tickLib.h"
#include "taskLib.h"

STATUS kernelTimeSlice(int ticks)
{
	struct timespec quantum;
	struct wind_task *task;

	/* Convert VxWorks ticks to timespec. */
	clockobj_ticks_to_timespec(&wind_clock, ticks, &quantum);

	/*
	 * XXX: Enable/disable round-robin for all threads known by
	 * the current process. Round-robin is most commonly about
	 * having multiple threads getting an equal share of time for
	 * running the same bulk of code, so applying this policy
	 * session-wide to multiple Xenomai processes would not make
	 * much sense. I.e. one is better off having all those threads
	 * running within a single process.
	 */
	wind_time_slice = ticks;
	do_each_wind_task(task, threadobj_set_rr(&task->thobj, &quantum));

	return OK;
}

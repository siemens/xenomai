/*
 * Copyright (C) 2011 Philippe Gerum <rpm@xenomai.org>.
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
#include <getopt.h>
#include <copperplate/init.h>
#include "timer.h"
#include "task.h"
#include "sem.h"
#include "event.h"
#include "cond.h"
#include "mutex.h"
#include "queue.h"
#include "buffer.h"
#include "heap.h"
#include "alarm.h"

static unsigned int clock_resolution = 1; /* nanosecond. */

static const struct option alchemy_options[] = {
	{
#define clock_resolution_opt	0
		.name = "alchemy-clock-resolution",
		.has_arg = 1,
		.flag = NULL,
		.val = 0
	},
	{
		.name = NULL,
		.has_arg = 0,
		.flag = NULL,
		.val = 0
	}
};

static int alchemy_init(int argc, char *const argv[])
{
	int ret, lindex, c;

	for (;;) {
		c = getopt_long_only(argc, argv, "", alchemy_options, &lindex);
		if (c == EOF)
			break;
		if (c > 0)
			continue;
		switch (lindex) {
		case clock_resolution_opt:
			clock_resolution = atoi(optarg);
			break;
		}
	}

	syncluster_init(&alchemy_task_table, "alchemy.task");
	syncluster_init(&alchemy_sem_table, "alchemy.sem");
	syncluster_init(&alchemy_event_table, "alchemy.event");
	syncluster_init(&alchemy_cond_table, "alchemy.cond");
	syncluster_init(&alchemy_mutex_table, "alchemy.mutex");
	syncluster_init(&alchemy_queue_table, "alchemy.queue");
	syncluster_init(&alchemy_buffer_table, "alchemy.buffer");
	syncluster_init(&alchemy_heap_table, "alchemy.heap");
	pvcluster_init(&alchemy_alarm_table, "alchemy.alarm");

	ret = clockobj_init(&alchemy_clock, "alchemy", clock_resolution);
	if (ret) {
		warning("%s: failed to initialize Alchemy clock (res=%u ns)",
			__FUNCTION__, clock_resolution);
		return __bt(ret);
	}

	return 0;
}

static struct copperskin alchemy_skin = {
	.name = "alchemy",
	.init = alchemy_init,
};

static __attribute__ ((constructor)) void register_alchemy(void)
{
	copperplate_register_skin(&alchemy_skin);
}

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
#include <getopt.h>
#include <assert.h>
#include <copperplate/init.h>
#include <vxworks/errnoLib.h>
#include "tickLib.h"
#include "taskLib.h"

static unsigned int clock_resolution = 1000000; /* 1ms */

static const struct option vxworks_options[] = {
	{
#define clock_resolution_opt	0
		.name = "vxworks-clock-resolution",
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

static int vxworks_init(int argc, char *const argv[])
{
	int ret, lindex, c;

	for (;;) {
		c = getopt_long_only(argc, argv, "", vxworks_options, &lindex);
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

	registry_add_dir("/vxworks");
	registry_add_dir("/vxworks/tasks");
	registry_add_dir("/vxworks/semaphores");
	registry_add_dir("/vxworks/queues");
	registry_add_dir("/vxworks/watchdogs");

	cluster_init(&wind_task_table, "vxworks.task");

	ret = clockobj_init(&wind_clock, "vxworks", clock_resolution);
	if (ret) {
		warning("%s: failed to initialize VxWorks clock (res=%u ns)",
			__FUNCTION__, clock_resolution);
		return ret;
	}

	return 0;
}

static struct copperskin vxworks_skin = {
	.name = "vxworks",
	.init = vxworks_init,
};

static __attribute__ ((constructor)) void register_vxworks(void)
{
	copperplate_register_skin(&vxworks_skin);
}

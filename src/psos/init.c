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
#include <copperplate/registry.h>
#include <copperplate/clockobj.h>
#include <psos/psos.h>
#include "tm.h"
#include "task.h"
#include "sem.h"
#include "queue.h"
#include "pt.h"
#include "rn.h"

u_long PSOS_INIT(int argc, char *const argv[])
{
	int ret;

	ret = copperplate_init(argc, argv);
	if (ret)
		return ret;
	
	registry_add_dir("/psos");
	registry_add_dir("/psos/tasks");
	registry_add_dir("/psos/semaphores");
	registry_add_dir("/psos/queues");
	registry_add_dir("/psos/timers");
	registry_add_dir("/psos/partitions");
	registry_add_dir("/psos/regions");

	cluster_init(&psos_task_table, "psos.task");
	cluster_init(&psos_sem_table, "psos.sema4");
	cluster_init(&psos_queue_table, "psos.queue");
	pvcluster_init(&psos_pt_table, "psos.pt");
	pvcluster_init(&psos_rn_table, "psos.rn");

	ret = clockobj_init(&psos_clock,
			    "psos", __tick_period_arg * 1000);
	if (ret) {
		warning("%s: failed to initialize pSOS clock (period=%uus)",
			__FUNCTION__, __tick_period_arg);
		return ret;
	}

	/* FIXME: this default 10-ticks value should be user-settable */
	clockobj_ticks_to_timeout(&psos_clock, 10, &psos_rrperiod);

	return SUCCESS;
}

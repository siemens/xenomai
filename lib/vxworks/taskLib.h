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

#ifndef _VXWORKS_TASKLIB_H
#define _VXWORKS_TASKLIB_H

#include <copperplate/threadobj.h>
#include <copperplate/registry.h>
#include <copperplate/cluster.h>
#include <vxworks/taskLib.h>

struct wind_task_args {

	FUNCPTR entry;
	long arg0;
	long arg1;
	long arg2;
	long arg3;
	long arg4;
	long arg5;
	long arg6;
	long arg7;
	long arg8;
	long arg9;
};

struct wind_task {

	pthread_mutex_t safelock;

	struct WIND_TCB *tcb;
	struct WIND_TCB priv_tcb;

	char name[32];
	struct wind_task_args args;

	struct threadobj thobj;
	struct fsobj fsobj;
	struct clusterobj cobj;
};

int wind_task_get_priority(struct wind_task *task);

#define task_magic	0x1a2b3c4d

static inline struct wind_task *wind_task_current(void)
{
	struct threadobj *thobj = threadobj_current();

	if (thobj == NULL ||
	    threadobj_get_magic(thobj) != task_magic)
		return NULL;

	return container_of(thobj, struct wind_task, thobj);
}

struct wind_task *get_wind_task(TASK_ID tid);

struct wind_task *get_wind_task_or_self(TASK_ID tid);

void put_wind_task(struct wind_task *task);

extern struct cluster wind_task_table;

#endif /* _VXWORKS_TASKLIB_H */

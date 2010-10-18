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

#include <pthread.h>
#include <string.h>
#include <copperplate/threadobj.h>
#include <vxworks/errnoLib.h>
#include <vxworks/taskInfo.h>
#include "taskLib.h"

const char *taskName(TASK_ID task_id)
{
	struct wind_task *task = get_wind_task_or_self(task_id);
	const char *name;

	if (task == NULL)
		return NULL;

	name = task->name;

	put_wind_task(task);

	/*
	 * This is unsafe, but this service is terminally flawed by
	 * design anyway.
	 */
	return name;
}

TASK_ID taskIdDefault(TASK_ID task_id)
{
	static TASK_ID value;

	if (task_id)
		value = task_id;

	return value;
}

TASK_ID taskNameToId(const char *name)
{
	struct wind_task *task;
	struct pvhashobj *obj;

	obj = pvhash_search(&wind_task_table, name);
	if (obj == NULL)
		return ERROR;

	task = container_of(obj, struct wind_task, obj);

	return (TASK_ID)task->tcb;
}

BOOL taskIsReady(TASK_ID task_id)
{
	struct wind_task *task = get_wind_task(task_id);
	int status;

	if (task == NULL)
		return 0;

	status = task->tcb->status;

	put_wind_task(task);

	return (status & (WIND_SUSPEND|WIND_DELAY)) == 0;
}

BOOL taskIsSuspended(TASK_ID task_id)
{
	struct wind_task *task = get_wind_task(task_id);
	int status;

	if (task == NULL)
		return 0;

	status = task->tcb->status;

	put_wind_task(task);

	return (status & WIND_SUSPEND) != 0;
}

STATUS taskGetInfo(TASK_ID task_id, TASK_DESC *desc)
{
	struct wind_task *task = get_wind_task(task_id);
	struct WIND_TCB *tcb;
	pthread_attr_t attr;
	int vfirst, vlast;
	size_t stacksize;
	void *stackbase;

	if (task == NULL) {
		errno = S_objLib_OBJ_ID_ERROR;
		return ERROR;
	}

	tcb = task->tcb;
	desc->td_tid = task_id;
	desc->td_priority = wind_task_get_priority(task);
	desc->td_status = tcb->status;
	desc->td_flags = tcb->flags;
	strncpy(desc->td_name, task->name, sizeof(desc->td_name));
	desc->td_entry = tcb->entry;
	desc->td_errorStatus = *task->thobj.errno_pointer;
	pthread_getattr_np(task->thobj.tid, &attr);
	put_wind_task(task);

	pthread_attr_getstack(&attr, &stackbase, &stacksize);
	desc->td_stacksize = stacksize;
	desc->td_pStackBase = stackbase;

	if (&vfirst < &vlast)
		/* Stack grows upward. */
		desc->td_pStackEnd = (caddr_t)stackbase + stacksize;
	else
		/* Stack grows downward. */
		desc->td_pStackEnd = (caddr_t)stackbase - stacksize;

	return OK;
}

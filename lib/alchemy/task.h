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

#ifndef _ALCHEMY_TASK_H
#define _ALCHEMY_TASK_H

#include <sched.h>
#include <semaphore.h>
#include <errno.h>
#include <boilerplate/list.h>
#include <copperplate/syncobj.h>
#include <copperplate/threadobj.h>
#include <copperplate/registry.h>
#include <copperplate/cluster.h>
#include <alchemy/task.h>

struct alchemy_task {
	char name[XNOBJECT_NAME_LEN];
	int mode;
	cpu_set_t affinity;
	int suspends;
	struct syncobj sobj_msg;
	int flowgen;
	struct threadobj thobj;
	struct clusterobj cobj;
	void (*entry)(void *arg);
	void *arg;
	RT_TASK self;
	struct fsobj fsobj;
};

struct alchemy_task_wait {
	struct RT_TASK_MCB request;
	struct RT_TASK_MCB reply;
};

#define task_magic	0x8282ebeb

static inline struct alchemy_task *alchemy_task_current(void)
{
	struct threadobj *thobj = threadobj_current();

	if (thobj == NULL ||
	    threadobj_get_magic(thobj) != task_magic)
		return NULL;

	return container_of(thobj, struct alchemy_task, thobj);
}

struct alchemy_task *get_alchemy_task(RT_TASK *task, int *err_r);

struct alchemy_task *get_alchemy_task_or_self(RT_TASK *task, int *err_r);

void put_alchemy_task(struct alchemy_task *tcb);

static inline int check_task_priority(int prio)
{				/* FIXME: HIPRIO is lower over Mercury */
	return prio < T_LOPRIO || prio > T_HIPRIO ? -EINVAL : 0;
}

extern struct syncluster alchemy_task_table;

#endif /* _ALCHEMY_TASK_H */

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

#ifndef _PSOS_TASK_H
#define _PSOS_TASK_H

#include <semaphore.h>
#include <copperplate/threadobj.h>
#include <copperplate/syncobj.h>
#include <copperplate/hash.h>
#include <copperplate/cluster.h>

struct psos_task_args {

	void (*entry)(u_long a0, u_long a1, u_long a2, u_long a3);
	u_long arg0;
	u_long arg1;
	u_long arg2;
	u_long arg3;
};

#define PSOSTASK_NR_REGS  16

struct psos_task {

	sem_t barrier;

	int flags;
	int mode;
	u_long events;
	u_long notepad[PSOSTASK_NR_REGS];
	struct pvlist timer_list; /* Private. Never accessed remotely. */

	char name[32];
	struct psos_task_args args;

	struct threadobj thobj;
	struct syncobj sobj;	/* For events. */
	struct clusterobj cobj;
};

int psos_task_get_priority(struct psos_task *task);

/*
 * pSOS priorities are mapped 1:1 to SCHED_FIFO levels:
 *
 * 1  -> SCHED_FIFO[1]
 * 97 -> SCHED_FIFO[97]
 * pSOS priorities above 97 are not supported so far.
 *
 */

static inline int psos_task_normalize_priority(int psos_prio)
{
	/* Rescale a pSOS priority level to a SCHED_FIFO one. */
	return psos_prio;
}

static inline int psos_task_denormalize_priority(int posix_prio)
{
	/* Rescale a SCHED_FIFO priority level to a pSOS one. */
	return posix_prio;
}

#define task_magic	0x8181fafa

static inline struct psos_task *psos_task_current(void)
{
	struct threadobj *thobj = threadobj_current();

	if (thobj == NULL ||
	    threadobj_get_magic(thobj) != task_magic)
		return NULL;

	return container_of(thobj, struct psos_task, thobj);
}

struct psos_task *get_psos_task(u_long tid, int *err_r);

struct psos_task *get_psos_task_or_self(u_long tid, int *err_r);

void put_psos_task(struct psos_task *task);

extern struct cluster psos_task_table;

#endif /* _PSOS_TASK_H */

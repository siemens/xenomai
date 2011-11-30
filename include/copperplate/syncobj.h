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

#ifndef _COPPERPLATE_SYNCOBJ_H
#define _COPPERPLATE_SYNCOBJ_H

#include <pthread.h>
#include <copperplate/list.h>
#include <copperplate/lock.h>

/* syncobj->flags */
#define SYNCOBJ_FIFO	0x0
#define SYNCOBJ_PRIO	0x1

/* threadobj->wait_status */
#define SYNCOBJ_DELETED		0x1
#define SYNCOBJ_FLUSHED		0x2
#define SYNCOBJ_BROADCAST	0x4
#define SYNCOBJ_DRAINING	0x8

#define SYNCOBJ_RELEASE_MASK	\
	(SYNCOBJ_DELETED|SYNCOBJ_FLUSHED|SYNCOBJ_BROADCAST)

/* threadobj->wait_hook(status) */
#define SYNCOBJ_BLOCK	0x1
#define SYNCOBJ_RESUME	0x2

struct threadobj;

struct syncstate {
	int state;
};

#ifdef CONFIG_XENO_COBALT

struct syncobj_corespec {
	cobalt_monitor_t monitor;
};

#else  /* CONFIG_XENO_MERCURY */

struct syncobj_corespec {
	pthread_mutex_t lock;
	pthread_cond_t drain_sync;
};

#endif /* CONFIG_XENO_MERCURY */

struct syncobj {
	int flags;
	int release_count;
	struct list pend_list;
	int pend_count;
	struct list drain_list;
	int drain_count;
	struct syncobj_corespec core;
	fnref_type(void (*)(struct syncobj *sobj)) finalizer;
};

#define syncobj_for_each_waiter(sobj, pos)		\
	list_for_each_entry(pos, &(sobj)->pend_list, wait_link)

#define syncobj_for_each_waiter_safe(sobj, pos, tmp)	\
	list_for_each_entry_safe(pos, tmp, &(sobj)->pend_list, wait_link)

void __syncobj_cleanup_wait(struct syncobj *sobj,
			    struct threadobj *thobj);
#ifdef __cplusplus
extern "C" {
#endif

void syncobj_init(struct syncobj *sobj, int flags,
		  fnref_type(void (*)(struct syncobj *sobj)) finalizer);

int syncobj_pend(struct syncobj *sobj,
		 const struct timespec *timeout,
		 struct syncstate *syns);

struct threadobj *syncobj_post(struct syncobj *sobj);

struct threadobj *syncobj_peek_at_pend(struct syncobj *sobj);

struct threadobj *syncobj_peek_at_drain(struct syncobj *sobj);

int syncobj_lock(struct syncobj *sobj,
		 struct syncstate *syns);

void syncobj_unlock(struct syncobj *sobj,
		    struct syncstate *syns);

int syncobj_wait_drain(struct syncobj *sobj,
		       const struct timespec *timeout,
		       struct syncstate *syns);

int __syncobj_signal_drain(struct syncobj *sobj);

static inline int syncobj_pended_p(struct syncobj *sobj)
{
	return !list_empty(&sobj->pend_list);
}

static inline int syncobj_pend_count(struct syncobj *sobj)
{
	return sobj->pend_count;
}

static inline int syncobj_drain_count(struct syncobj *sobj)
{
	return sobj->drain_count;
}

void syncobj_requeue_waiter(struct syncobj *sobj, struct threadobj *thobj);

void syncobj_wakeup_waiter(struct syncobj *sobj, struct threadobj *thobj);

int syncobj_flush(struct syncobj *sobj, int reason);

int syncobj_destroy(struct syncobj *sobj,
		    struct syncstate *syns);

void syncobj_uninit(struct syncobj *sobj);

#ifdef __cplusplus
}
#endif

static inline int syncobj_signal_drain(struct syncobj *sobj)
{
	int ret = 0;

	if (sobj->drain_count > 0)
		ret = __syncobj_signal_drain(sobj);

	return ret;
}

#endif /* _COPPERPLATE_SYNCOBJ_H */

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

/* syncobj->flags */
#define SYNCOBJ_FIFO	0x0
#define SYNCOBJ_PRIO	0x1

/* threadobj->wait_status */
#define SYNCOBJ_DELETED	0x1
#define SYNCOBJ_FLUSHED	0x2

/* threadobj->wait_hook(status) */
#define SYNCOBJ_BLOCK	0x1
#define SYNCOBJ_RESUME	0x2

struct syncobj {
	int flags;
	int release_count;
	int cancel_type;
	pthread_mutex_t lock;
	pthread_cond_t post_sync;
	struct list pend_list;
	struct list drain_list;
	int drain_count;
	fnref_type(void (*)(struct syncobj *sobj)) finalizer;
};

#define syncobj_for_each_waiter(sobj, pos)		\
	list_for_each_entry(pos, &(sobj)->pend_list, wait_link)

#define syncobj_for_each_waiter_safe(sobj, pos, tmp)	\
	list_for_each_entry_safe(pos, tmp, &(sobj)->pend_list, wait_link)

#ifdef __cplusplus
extern "C" {
#endif

void syncobj_init(struct syncobj *sobj, int flags,
		  fnref_type(void (*)(struct syncobj *sobj)) finalizer);

int syncobj_pend(struct syncobj *sobj, struct timespec *timeout);

struct threadobj *syncobj_post(struct syncobj *sobj);

int syncobj_wait_drain(struct syncobj *sobj, struct timespec *timeout);

int __syncobj_signal_drain(struct syncobj *sobj);

void syncobj_requeue_waiter(struct syncobj *sobj, struct threadobj *thobj);

void syncobj_wakeup_waiter(struct syncobj *sobj, struct threadobj *thobj);

int syncobj_lock(struct syncobj *sobj);

void syncobj_unlock(struct syncobj *sobj);

int syncobj_flush(struct syncobj *sobj, int reason);

int syncobj_destroy(struct syncobj *sobj);

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

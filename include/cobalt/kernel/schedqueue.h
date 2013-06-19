/*
 * Copyright (C) 2008 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _COBALT_KERNEL_SCHEDQUEUE_H
#define _COBALT_KERNEL_SCHEDQUEUE_H

#include <cobalt/kernel/list.h>

#define XNSCHED_CLASS_MAX_PRIO		1024

#ifdef CONFIG_XENO_OPT_SCALABLE_SCHED
/*
 * Multi-level priority queue, suitable for handling the runnable
 * thread queue of a scheduling class with O(1) property. We only
 * manage a descending queuing order, i.e. highest numbered priorities
 * come first.
 */
#define XNSCHED_MLQ_LEVELS  264

#if BITS_PER_LONG * BITS_PER_LONG < XNSCHED_MLQ_LEVELS
#error "internal bitmap cannot hold so many priority levels"
#endif

#define __MLQ_LONGS ((XNSCHED_MLQ_LEVELS+BITS_PER_LONG-1)/BITS_PER_LONG)

struct xnsched_mlq {
	int loprio, hiprio, elems;
	unsigned long himap, lomap[__MLQ_LONGS];
	struct list_head heads[XNSCHED_MLQ_LEVELS];
};

#undef __MLQ_LONGS

struct xnthread;

void sched_initq(struct xnsched_mlq *q,
		 int loprio, int hiprio);

void sched_insertqff(struct xnsched_mlq *q, 
		     struct xnthread *thread);

void sched_insertqlf(struct xnsched_mlq *q,
		     struct xnthread *thread);

void sched_removeq(struct xnsched_mlq *q,
		   struct xnthread *thread);

struct xnthread *sched_getq(struct xnsched_mlq *q);

static inline int sched_emptyq_p(struct xnsched_mlq *q)
{
	return q->himap == 0;
}

typedef struct xnsched_mlq xnsched_queue_t;

#else /* ! CONFIG_XENO_OPT_SCALABLE_SCHED */

typedef struct list_head xnsched_queue_t;

#define sched_initq(__q, __minp, __maxp)	INIT_LIST_HEAD(__q)
#define sched_emptyq_p(__q)			list_empty(__q)
#define sched_insertqlf(__q, __t)		list_add_prilf(__t, __q, cprio, rlink)
#define sched_insertqff(__q, __t)		list_add_priff(__t, __q, cprio, rlink)
#define sched_removeq(__q, __t)			list_del(&(__t)->rlink)
#define sched_getq(__q)								\
	({									\
		struct xnthread *__t = NULL;					\
		if (!list_empty(__q))						\
			__t = list_get_entry(__q, struct xnthread, rlink);	\
		__t;								\
	})

#endif /* !CONFIG_XENO_OPT_SCALABLE_SCHED */

struct xnthread *sched_findq(xnsched_queue_t *q, int prio);

#endif /* !_COBALT_KERNEL_SCHEDQUEUE_H */

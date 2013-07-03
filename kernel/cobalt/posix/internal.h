/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _COBALT_POSIX_INTERNAL_H
#define _COBALT_POSIX_INTERNAL_H

#include <cobalt/kernel/pod.h>
#include <cobalt/kernel/heap.h>
#include <cobalt/kernel/ppd.h>
#include <cobalt/kernel/select.h>
#include <cobalt/kernel/assert.h>
#include <cobalt/kernel/list.h>
#include <cobalt/uapi/syscall.h>
#include <asm/xenomai/syscall.h>
#include <asm/xenomai/arith.h>
#include "registry.h"

#ifndef CONFIG_XENO_OPT_DEBUG_COBALT
#define CONFIG_XENO_OPT_DEBUG_COBALT 0
#endif

#define COBALT_MAGIC(n) (0x8686##n##n)
#define COBALT_ANY_MAGIC         COBALT_MAGIC(00)
#define COBALT_THREAD_MAGIC      COBALT_MAGIC(01)
#define COBALT_THREAD_ATTR_MAGIC COBALT_MAGIC(02)
#define COBALT_MUTEX_ATTR_MAGIC  (COBALT_MAGIC(04) & ((1 << 24) - 1))
#define COBALT_COND_ATTR_MAGIC   (COBALT_MAGIC(06) & ((1 << 24) - 1))
#define COBALT_KEY_MAGIC         COBALT_MAGIC(08)
#define COBALT_ONCE_MAGIC        COBALT_MAGIC(09)
#define COBALT_MQ_MAGIC          COBALT_MAGIC(0A)
#define COBALT_MQD_MAGIC         COBALT_MAGIC(0B)
#define COBALT_INTR_MAGIC        COBALT_MAGIC(0C)
#define COBALT_TIMER_MAGIC       COBALT_MAGIC(0E)
#define COBALT_EVENT_MAGIC       COBALT_MAGIC(0F)
#define COBALT_MONITOR_MAGIC     COBALT_MAGIC(10)

#define ONE_BILLION             1000000000

#define cobalt_obj_active(h,m,t)			\
	((h) && ((t *)(h))->magic == (m))

#define cobalt_obj_deleted(h,m,t)		\
	((h) && ((t *)(h))->magic == ~(m))

#define cobalt_mark_deleted(t) ((t)->magic = ~(t)->magic)

struct cobalt_kqueues {
	struct list_head condq;
	struct list_head mutexq;
	struct list_head semq;
	struct list_head threadq;
	struct list_head timerq;
	struct list_head monitorq;
	struct list_head eventq;
};

struct cobalt_context {
	struct cobalt_kqueues kqueues;
	cobalt_assocq_t uqds;
	cobalt_assocq_t usems;
	struct xnshadow_ppd ppd;
};

extern int cobalt_muxid;

extern struct cobalt_kqueues cobalt_global_kqueues;

static inline struct cobalt_context *cobalt_process_context(void)
{
	struct xnshadow_ppd *ppd;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	ppd = xnshadow_ppd_get(cobalt_muxid);
	xnlock_put_irqrestore(&nklock, s);

	if (ppd == NULL)
		return NULL;

	return container_of(ppd, struct cobalt_context, ppd);
}

static inline struct cobalt_kqueues *cobalt_kqueues(int pshared)
{
	struct xnshadow_ppd *ppd;

	if (pshared || (ppd = xnshadow_ppd_get(cobalt_muxid)) == NULL)
		return &cobalt_global_kqueues;

	return &container_of(ppd, struct cobalt_context, ppd)->kqueues;
}

static inline void ns2ts(struct timespec *ts, xnticks_t nsecs)
{
	ts->tv_sec = xnclock_divrem_billion(nsecs, &ts->tv_nsec);
}

static inline xnticks_t ts2ns(const struct timespec *ts)
{
	xntime_t nsecs = ts->tv_nsec;

	if (ts->tv_sec)
		nsecs += (xntime_t)ts->tv_sec * ONE_BILLION;

	return nsecs;
}

static inline xnticks_t tv2ns(const struct timeval *tv)
{
	xntime_t nsecs = tv->tv_usec * 1000;

	if (tv->tv_sec)
		nsecs += (xntime_t)tv->tv_sec * ONE_BILLION;

	return nsecs;
}

static inline void ticks2tv(struct timeval *tv, xnticks_t ticks)
{
	unsigned long nsecs;

	tv->tv_sec = xnclock_divrem_billion(ticks, &nsecs);
	tv->tv_usec = nsecs / 1000;
}

static inline xnticks_t clock_get_ticks(clockid_t clock_id)
{
	return clock_id == CLOCK_REALTIME ?
		xnclock_read() :
		xnclock_read_monotonic();
}

static inline int clock_flag(int flag, clockid_t clock_id)
{
	switch(flag & TIMER_ABSTIME) {
	case 0:
		return XN_RELATIVE;

	case TIMER_ABSTIME:
		switch(clock_id) {
		case CLOCK_MONOTONIC:
		case CLOCK_MONOTONIC_RAW:
			return XN_ABSOLUTE;

		case CLOCK_REALTIME:
			return XN_REALTIME;
		}
	}
	return -EINVAL;
}

int cobalt_mq_elect_bind(mqd_t fd, struct xnselector *selector,
			 unsigned type, unsigned index);

int cobalt_init(void);

void cobalt_cleanup(void);

int cobalt_syscall_init(void);

void cobalt_syscall_cleanup(void);

#endif /* !_COBALT_POSIX_INTERNAL_H */

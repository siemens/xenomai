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

#ifndef _POSIX_INTERNAL_H
#define _POSIX_INTERNAL_H

#include <nucleus/xenomai.h>
#include <nucleus/ppd.h>
#include <nucleus/select.h>
#include <cobalt/posix.h>
#include "registry.h"

/* debug support */
#include <nucleus/assert.h>

#ifndef CONFIG_XENO_OPT_DEBUG_POSIX
#define CONFIG_XENO_OPT_DEBUG_POSIX 0
#endif

#define COBALT_MAGIC(n) (0x8686##n##n)
#define COBALT_ANY_MAGIC         COBALT_MAGIC(00)
#define COBALT_THREAD_MAGIC      COBALT_MAGIC(01)
#define COBALT_THREAD_ATTR_MAGIC COBALT_MAGIC(02)
#define COBALT_MUTEX_MAGIC       COBALT_MAGIC(03)
#define COBALT_MUTEX_ATTR_MAGIC  (COBALT_MAGIC(04) & ((1 << 24) - 1))
#define COBALT_COND_MAGIC        COBALT_MAGIC(05)
#define COBALT_COND_ATTR_MAGIC   (COBALT_MAGIC(05) & ((1 << 24) - 1))
#define COBALT_SEM_MAGIC         COBALT_MAGIC(06)
#define COBALT_KEY_MAGIC         COBALT_MAGIC(07)
#define COBALT_ONCE_MAGIC        COBALT_MAGIC(08)
#define COBALT_MQ_MAGIC          COBALT_MAGIC(09)
#define COBALT_MQD_MAGIC         COBALT_MAGIC(0A)
#define COBALT_INTR_MAGIC        COBALT_MAGIC(0B)
#define COBALT_NAMED_SEM_MAGIC   COBALT_MAGIC(0C)
#define COBALT_TIMER_MAGIC       COBALT_MAGIC(0D)
#define COBALT_SHM_MAGIC         COBALT_MAGIC(0E)

#define COBALT_MIN_PRIORITY      XNSCHED_LOW_PRIO
#define COBALT_MAX_PRIORITY      XNSCHED_HIGH_PRIO

#define ONE_BILLION             1000000000

#define cobalt_obj_active(h,m,t)			\
	((h) && ((t *)(h))->magic == (m))

#define cobalt_obj_deleted(h,m,t)		\
	((h) && ((t *)(h))->magic == ~(m))

#define cobalt_mark_deleted(t) ((t)->magic = ~(t)->magic)

typedef struct {
	xnqueue_t condq;
	xnqueue_t intrq;
	xnqueue_t mutexq;
	xnqueue_t semq;
	xnqueue_t threadq;
	xnqueue_t timerq;
} cobalt_kqueues_t;

#ifndef __XENO_SIM__
typedef struct {
	cobalt_kqueues_t kqueues;
	cobalt_assocq_t uqds;
	cobalt_assocq_t usems;
	cobalt_assocq_t umaps;
	cobalt_assocq_t ufds;

	xnshadow_ppd_t ppd;

#define ppd2queues(addr)						\
	((cobalt_queues_t *) ((char *) (addr) - offsetof(cobalt_queues_t, ppd)))

} cobalt_queues_t;

extern int cobalt_muxid;
#endif /* !__XENO_SIM__ */

extern cobalt_kqueues_t cobalt_global_kqueues;

#ifndef __XENO_SIM__
static inline cobalt_queues_t *cobalt_queues(void)
{
	xnshadow_ppd_t *ppd;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	ppd = xnshadow_ppd_get(cobalt_muxid);

	xnlock_put_irqrestore(&nklock, s);

	if (!ppd)
		return NULL;

	return ppd2queues(ppd);
}
#endif /* !__XENO_SIM__ */

static inline cobalt_kqueues_t *cobalt_kqueues(int pshared)
{
#ifndef __XENO_SIM__
	xnshadow_ppd_t *ppd;

	if (pshared || !(ppd = xnshadow_ppd_get(cobalt_muxid)))
		return &cobalt_global_kqueues;

	return &ppd2queues(ppd)->kqueues;
#else /* __XENO_SIM__ */
	return &cobalt_global_kqueues;
#endif /* __XENO_SIM__ */
}

static inline void ns2ts(struct timespec *ts, xnticks_t nsecs)
{
	ts->tv_sec = xnarch_divrem_billion(nsecs, &ts->tv_nsec);
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

	tv->tv_sec = xnarch_divrem_billion(ticks, &nsecs);
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

int cobalt_mq_select_bind(mqd_t fd, struct xnselector *selector,
			 unsigned type, unsigned index);

#endif /* !_POSIX_INTERNAL_H */

/**
 * @file
 * @note Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * \ingroup timer
 */

#ifndef _COBALT_KERNEL_TIMER_H
#define _COBALT_KERNEL_TIMER_H

#include <cobalt/kernel/clock.h>
#include <cobalt/kernel/stat.h>
#include <cobalt/kernel/list.h>

#ifndef CONFIG_XENO_OPT_DEBUG_TIMERS
#define CONFIG_XENO_OPT_DEBUG_TIMERS  0
#endif

#define XN_INFINITE   ((xnticks_t)0)
#define XN_NONBLOCK   ((xnticks_t)-1)

/* Timer modes */
typedef enum xntmode {
	XN_RELATIVE,
	XN_ABSOLUTE,
	XN_REALTIME
} xntmode_t;

#define XNTIMER_WHEELSIZE 64
#define XNTIMER_WHEELMASK (XNTIMER_WHEELSIZE - 1)

/* Timer status */
#define XNTIMER_DEQUEUED  0x00000001
#define XNTIMER_KILLED    0x00000002
#define XNTIMER_PERIODIC  0x00000004
#define XNTIMER_REALTIME  0x00000008
#define XNTIMER_FIRED     0x00000010
#define XNTIMER_NOBLCK	  0x00000020

/* These flags are available to the real-time interfaces */
#define XNTIMER_SPARE0  0x01000000
#define XNTIMER_SPARE1  0x02000000
#define XNTIMER_SPARE2  0x04000000
#define XNTIMER_SPARE3  0x08000000
#define XNTIMER_SPARE4  0x10000000
#define XNTIMER_SPARE5  0x20000000
#define XNTIMER_SPARE6  0x40000000
#define XNTIMER_SPARE7  0x80000000

/* Timer priorities */
#define XNTIMER_LOPRIO  (-999999999)
#define XNTIMER_STDPRIO 0
#define XNTIMER_HIPRIO  999999999

#define XNTIMER_KEEPER_ID 0

struct xntlholder {
	struct list_head link;
	xnticks_t key;
	int prio;
};

#define xntlholder_date(h)	((h)->key)
#define xntlholder_prio(h)	((h)->prio)
#define xntlist_init(q)		INIT_LIST_HEAD(q)
#define xntlist_head(q)							\
	({								\
		struct xntlholder *h = list_empty(q) ? NULL :		\
			list_first_entry(q, struct xntlholder, link);	\
		h;							\
	})

#define xntlist_next(q, h)						\
	({								\
		struct xntlholder *_h = list_is_last(&h->link, q) ? NULL : \
			list_entry(h->link.next, struct xntlholder, link); \
		_h;							\
	})

static inline void xntlist_insert(struct list_head *q, struct xntlholder *holder)
{
	struct xntlholder *p;

	if (list_empty(q)) {
		list_add(&holder->link, q);
		return;
	}

	/*
	 * Insert the new timer at the proper place in the single
	 * queue. O(N) here, but this is the price for the increased
	 * flexibility...
	 */
	list_for_each_entry_reverse(p, q, link) {
		if ((xnsticks_t) (holder->key - p->key) > 0 ||
		    (holder->key == p->key && holder->prio <= p->prio))
		  break;
	}

	list_add(&holder->link, &p->link);
}

#define xntlist_remove(q, h)			\
	do {					\
		(void)(q);			\
		list_del(&(h)->link);		\
	} while (0)

#if defined(CONFIG_XENO_OPT_TIMER_HEAP)

#include <cobalt/kernel/bheap.h>

typedef bheaph_t xntimerh_t;

#define xntimerh_date(h)          bheaph_key(h)
#define xntimerh_prio(h)          bheaph_prio(h)
#define xntimerh_init(h)          bheaph_init(h)

typedef DECLARE_BHEAP_CONTAINER(xntimerq_t, CONFIG_XENO_OPT_TIMER_HEAP_CAPACITY);

#define xntimerq_init(q)          bheap_init((q), CONFIG_XENO_OPT_TIMER_HEAP_CAPACITY)
#define xntimerq_destroy(q)       bheap_destroy(q)
#define xntimerq_head(q)          bheap_gethead(q)
#define xntimerq_insert(q, h)     bheap_insert((q),(h))
#define xntimerq_remove(q, h)     bheap_delete((q),(h))

typedef struct {} xntimerq_it_t;

#define xntimerq_it_begin(q, i)   ((void) (i), bheap_gethead(q))
#define xntimerq_it_next(q, i, h) ((void) (i), bheap_next((q),(h)))

#else /* CONFIG_XENO_OPT_TIMER_LIST */

typedef struct xntlholder xntimerh_t;

#define xntimerh_date(h)       xntlholder_date(h)
#define xntimerh_prio(h)       xntlholder_prio(h)
#define xntimerh_init(h)       do { } while (0)

typedef struct list_head xntimerq_t;

#define xntimerq_init(q)        xntlist_init(q)
#define xntimerq_destroy(q)     do { } while (0)
#define xntimerq_head(q)        xntlist_head(q)
#define xntimerq_insert(q,h)    xntlist_insert((q),(h))
#define xntimerq_remove(q, h)   xntlist_remove((q),(h))

typedef struct { } xntimerq_it_t;

#define xntimerq_it_begin(q,i)  ((void) (i), xntlist_head(q))
#define xntimerq_it_next(q,i,h) ((void) (i), xntlist_next((q),(h)))

#endif /* CONFIG_XENO_OPT_TIMER_LIST */

struct xnsched;

typedef struct xntimer {

	xntimerh_t aplink;	/* Link in timers list. */
#define aplink2timer(ln) container_of(ln, xntimer_t, aplink)

	struct list_head adjlink;

	unsigned long status;	/* !< Timer status. */

	xnticks_t interval;	/* !< Periodic interval (in ticks, 0 == one shot). */

	xnticks_t pexpect;	/* !< Date of next periodic release point (raw ticks). */

	struct xnsched *sched;	/* !< Sched structure to which the timer is
				   attached. */

	void (*handler)(struct xntimer *timer); /* !< Timeout handler. */

#ifdef CONFIG_XENO_OPT_STATS
	char name[XNOBJECT_NAME_LEN]; /* !< Timer name to be displayed. */
	const char *handler_name; /* !< Handler name to be displayed. */
	struct list_head tblink; /* !< Timer holder in timebase. */
#endif /* CONFIG_XENO_OPT_STATS */

	xnstat_counter_t scheduled; /* !< Number of timer schedules. */

	xnstat_counter_t fired; /* !< Number of timer events. */

} xntimer_t;

#ifdef CONFIG_SMP
#define xntimer_sched(t)	((t)->sched)
#else /* !CONFIG_SMP */
#define xntimer_sched(t)	xnpod_current_sched()
#endif /* !CONFIG_SMP */
#define xntimer_interval(t)	((t)->interval)
#define xntimer_pexpect(t)      ((t)->pexpect)
#define xntimer_pexpect_forward(t,delta) ((t)->pexpect += delta)

#define xntimer_set_priority(t, p)			\
	do { xntimerh_prio(&(t)->aplink) = (p); } while(0)

static inline int xntimer_active_p (xntimer_t *timer)
{
	return timer->sched != NULL;
}

static inline int xntimer_running_p(xntimer_t *timer)
{
	return (timer->status & XNTIMER_DEQUEUED) == 0;
}

static inline int xntimer_reload_p(xntimer_t *timer)
{
	return (timer->status &
		(XNTIMER_PERIODIC|XNTIMER_DEQUEUED|XNTIMER_KILLED)) ==
		(XNTIMER_PERIODIC|XNTIMER_DEQUEUED);
}

#ifdef CONFIG_XENO_OPT_STATS
#define xntimer_init(timer, handler)			\
	do {						\
		__xntimer_init(timer, handler);		\
		(timer)->handler_name = #handler;	\
	} while (0)
#else /* !CONFIG_XENO_OPT_STATS */
#define xntimer_init	__xntimer_init
#endif /* !CONFIG_XENO_OPT_STATS */

#define xntimer_init_noblock(timer, handler)		\
	do {						\
		xntimer_init(timer, handler);		\
		(timer)->status |= XNTIMER_NOBLCK;	\
	} while(0)

void __xntimer_init(struct xntimer *timer,
		    void (*handler)(struct xntimer *timer));

void xntimer_destroy(xntimer_t *timer);

static inline void xntimer_set_name(xntimer_t *timer, const char *name)
{
#ifdef CONFIG_XENO_OPT_STATS
	strncpy(timer->name, name, sizeof(timer->name));
#endif /* CONFIG_XENO_OPT_STATS */
}

void xntimer_next_local_shot(struct xnsched *sched);

/*!
 * \addtogroup timer
 *@{ */

int xntimer_start(xntimer_t *timer,
		  xnticks_t value,
		  xnticks_t interval,
		  xntmode_t mode);

void __xntimer_stop(xntimer_t *timer);

xnticks_t xntimer_get_date(xntimer_t *timer);

xnticks_t xntimer_get_timeout(xntimer_t *timer);

xnticks_t xntimer_get_interval(xntimer_t *timer);

static inline void xntimer_stop(xntimer_t *timer)
{
	if ((timer->status & XNTIMER_DEQUEUED) == 0)
		__xntimer_stop(timer);
}

static inline xnticks_t xntimer_get_timeout_stopped(xntimer_t *timer)
{
	return xntimer_get_timeout(timer);
}

static inline xnticks_t xntimer_get_expiry(xntimer_t *timer)
{
	return xntimerh_date(&timer->aplink);
}

/*@}*/

void xntimer_init_proc(void);

void xntimer_cleanup_proc(void);

unsigned long xntimer_get_overruns(xntimer_t *timer, xnticks_t now);

void xntimer_freeze(void);

void xntimer_tick(void);

void xntimer_adjust_all(xnsticks_t delta);

#ifdef CONFIG_SMP
int xntimer_migrate(xntimer_t *timer, struct xnsched *sched);
#else /* ! CONFIG_SMP */
#define xntimer_migrate(timer, sched)	do { } while(0)
#endif /* CONFIG_SMP */

#define xntimer_set_sched(timer, sched)	xntimer_migrate(timer, sched)

char *xntimer_format_time(xnticks_t value,
			  char *buf, size_t bufsz);

int xntimer_grab_hardware(int cpu);

void xntimer_release_hardware(int cpu);

#endif /* !_COBALT_KERNEL_TIMER_H */

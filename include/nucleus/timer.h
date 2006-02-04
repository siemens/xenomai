/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
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
 */

#ifndef _XENO_NUCLEUS_TIMER_H
#define _XENO_NUCLEUS_TIMER_H

#include <nucleus/queue.h>

#if defined(__KERNEL__) || defined(__XENO_UVM__) || defined(__XENO_SIM__)

#ifdef CONFIG_XENO_OPT_TIMING_PERIODIC
/* Number of outstanding timers (hint only) -- must be ^2 */
#define XNTIMER_WHEELSIZE 64
#define XNTIMER_WHEELMASK (XNTIMER_WHEELSIZE - 1)
#else /* !CONFIG_XENO_OPT_TIMING_PERIODIC */
#define XNTIMER_WHEELSIZE 1
#endif /* CONFIG_XENO_OPT_TIMING_PERIODIC */

#define XNTIMER_ENABLED   0x00000001
#define XNTIMER_DEQUEUED  0x00000002
#define XNTIMER_KILLED    0x00000004

/* These flags are available to the real-time interfaces */
#define XNTIMER_SPARE0  0x01000000
#define XNTIMER_SPARE1  0x02000000
#define XNTIMER_SPARE2  0x04000000
#define XNTIMER_SPARE3  0x08000000
#define XNTIMER_SPARE4  0x10000000
#define XNTIMER_SPARE5  0x20000000
#define XNTIMER_SPARE6  0x40000000
#define XNTIMER_SPARE7  0x80000000

#define XNTIMER_LOPRIO  (-999999999)
#define XNTIMER_STDPRIO 0
#define XNTIMER_HIPRIO  999999999

#define XNTIMER_KEEPER_ID 0

struct xnsched;

typedef struct xntimer {

    xnholder_t link;

#define link2timer(laddr) \
((xntimer_t *)(((char *)laddr) - (int)(&((xntimer_t *)0)->link)))

    xnflags_t status;		/* !< Timer status. */

    xnticks_t date;		/* !< Absolute timeout date (in ticks). */

    xnticks_t interval;		/* !< Periodic interval (in ticks, 0 == one shot). */

    int prio;			/* !< Internal priority. */

    struct xnsched *sched;      /* !< Sched structure to which the timer is
                                   attached. */

    void (*handler)(void *cookie); /* !< Timeout handler. */

    void *cookie;	/* !< Cookie to pass to the timeout handler. */

    XNARCH_DECL_DISPLAY_CONTEXT();

} xntimer_t;

#define xntimer_date(t)           ((t)->date)
#if defined(CONFIG_SMP)
#define xntimer_sched(t)          ((t)->sched)
#else /* !CONFIG_SMP */
#define xntimer_sched(t)          xnpod_current_sched()
#endif /* !CONFIG_SMP */
#define xntimer_interval(t)       ((t)->interval)
#define xntimer_set_cookie(t,c)   ((t)->cookie = (c))
#define xntimer_set_priority(t,p) ((t)->prio = (p))

static inline int xntimer_active_p (xntimer_t *timer) {
    return timer->sched != NULL;
}

static inline int xntimer_running_p (xntimer_t *timer) {
    return !testbits(timer->status,XNTIMER_DEQUEUED);
}

typedef struct xntmops {

    void (*do_tick)(void);
    xnticks_t (*get_jiffies)(void);
    void (*do_timer_start)(xntimer_t *timer,
			   xnticks_t value,
			   xnticks_t interval);
    void (*do_timer_stop)(xntimer_t *timer);
    xnticks_t (*get_timer_date)(xntimer_t *timer);
    xnticks_t (*get_timer_timeout)(xntimer_t *timer);
    void (*set_timer_remote)(xntimer_t *timer);
    const char *(*get_type)(void);

} xntmops_t;

#ifdef __cplusplus
extern "C" {
#endif

extern xntmops_t *nktimer;

void xntimer_init(xntimer_t *timer,
		  void (*handler)(void *cookie),
		  void *cookie);

void xntimer_destroy(xntimer_t *timer);

void xntimer_start(xntimer_t *timer,
		   xnticks_t value,
		   xnticks_t interval);

/*!
 * \fn int xntimer_stop(xntimer_t *timer)
 *
 * \brief Disarm a timer.
 *
 * This service deactivates a timer previously armed using
 * xntimer_start(). Once disarmed, the timer can be subsequently
 * re-armed using the latter service.
 *
 * @param timer The address of a valid timer descriptor.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

static inline void xntimer_stop(xntimer_t *timer)
{
    if (!testbits(timer->status,XNTIMER_DEQUEUED))
	nktimer->do_timer_stop(timer);
}

void xntimer_freeze(void);

xnticks_t xntimer_get_date(xntimer_t *timer);

xnticks_t xntimer_get_timeout(xntimer_t *timer);

void xntimer_set_periodic_mode(void);

void xntimer_set_aperiodic_mode(void);

#if defined(CONFIG_SMP)
int xntimer_set_sched(xntimer_t *timer, struct xnsched *sched);
#else /* ! CONFIG_SMP */
#define xntimer_set_sched(timer,sched)
#endif /* CONFIG_SMP */

#ifdef __cplusplus
}
#endif

#endif /* __KERNEL__ || __XENO_UVM__ || __XENO_SIM__ */

#endif /* !_XENO_NUCLEUS_TIMER_H */

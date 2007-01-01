/*
 * Copyright (C) 2006,2007 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _XENO_NUCLEUS_TIMEBASE_H
#define _XENO_NUCLEUS_TIMEBASE_H

#include <nucleus/queue.h>

#if defined(__KERNEL__) || defined(__XENO_SIM__)

struct xntimer;

typedef struct xntbops {

    int (*start_timer)(struct xntimer *timer,
		       xnticks_t value,
		       xnticks_t interval,
		       int mode);
    void (*stop_timer)(struct xntimer *timer);
    xnticks_t (*get_timer_date)(struct xntimer *timer);
    xnticks_t (*get_timer_timeout)(struct xntimer *timer);
    xnticks_t (*get_timer_interval)(struct xntimer *timer);
    xnticks_t (*get_timer_raw_expiry)(struct xntimer *timer);
    void (*move_timer)(struct xntimer *timer);

} xntbops_t;

#define XNTBRUN  0x00000001	/* Time base is running. */
#define XNTBSET  0x00000002	/* Time set in time base. */
#define XNTBLCK  0x00000004	/* Time base is locked. */

typedef struct xntbase {

	struct xntbops *ops;	/*!< Time base operations. */

	xnticks_t jiffies;	/*!< Ticks elapsed since init (remains null if aperiodic). */

	void (*hook)(void);	/*!< Hook routine called upon tick. */

	xnticks_t wallclock_offset; /*!< (Wallclock time - epoch) in ticks. */

	u_long tickvalue;	/*!< Tick duration (ns, 1 if aperiodic). */

	u_long ticks2sec;	/*!< Number of ticks per second. */

	u_long status;		/*!< Status information. */

	const char *name;	/* !< Name of time base. */

	xnholder_t link;

#define link2tbase(ln)	container_of(ln, xntbase_t, link)

} xntbase_t;

#define xntbase_timeset_p(base)	(!!testbits((base)->status,XNTBSET))

#ifdef __cplusplus
extern "C" {
#endif

extern xntbase_t nktbase;

extern xnqueue_t nktimebaseq;

static inline u_long xntbase_get_ticks2sec(xntbase_t *base)
{
	return base->ticks2sec;
}

static inline u_long xntbase_get_tickval(xntbase_t *base)
{
	/* Returns the duration of a tick in nanoseconds */
	return base->tickvalue;
}

static inline void xntbase_set_hook(xntbase_t *base, void (*hook)(void))
{
	base->hook = hook;
}

static inline int xntbase_enabled_p(xntbase_t *base)
{
	return !!(base->status & XNTBRUN);
}

#ifdef CONFIG_XENO_OPT_TIMING_PERIODIC

static inline xntime_t xntbase_ticks2ns(xntbase_t *base, xnticks_t ticks)
{
	/* Convert a count of ticks in nanoseconds */
	return ticks * xntbase_get_tickval(base);
}

static inline xnticks_t xntbase_ns2ticks(xntbase_t *base, xntime_t t)
{
	return xnarch_ulldiv(t, xntbase_get_tickval(base), NULL);
}

static inline int xntbase_periodic_p(xntbase_t *base)
{
	return base->tickvalue != 1;
}

static inline int xntbase_master_p(xntbase_t *base)
{
	return base == &nktbase;
}

static inline xnticks_t xntbase_get_jiffies(xntbase_t *base)
{
	return base->jiffies;
}

static inline xnticks_t xntbase_get_rawclock(xntbase_t *base)
{
	return xntbase_get_jiffies(base);
}

int xntbase_alloc(const char *name,
		  u_long period,
		  xntbase_t **basep);

void xntbase_free(xntbase_t *base);

int xntbase_update(xntbase_t *base,
		   u_long period);

int xntbase_switch(const char *name,
		   u_long period,
		   xntbase_t **basep);

void xntbase_start(xntbase_t *base);

void xntbase_stop(xntbase_t *base);

void xntbase_tick(xntbase_t *base);

#else /* !CONFIG_XENO_OPT_TIMING_PERIODIC */

void xntimer_tick_aperiodic(void);

static inline xntime_t xntbase_ticks2ns(xntbase_t *base, xnticks_t ticks)
{
	return ticks;
}

static inline xnticks_t xntbase_ns2ticks(xntbase_t *base, xntime_t t)
{
	return t;
}

static inline int xntbase_periodic_p(xntbase_t *base)
{
	return 0;
}

static inline int xntbase_master_p(xntbase_t *base)
{
	return 1;
}

static inline xnticks_t xntbase_get_jiffies(xntbase_t *base)
{
	return xnarch_get_cpu_time();
}

static inline xnticks_t xntbase_get_rawclock(xntbase_t *base)
{
	return xnarch_get_cpu_tsc();
}

static inline int xntbase_alloc(const char *name, u_long period, xntbase_t **basep)
{
	*basep = &nktbase;
	return 0;
}

static inline void xntbase_free(xntbase_t *base)
{
}

static inline int xntbase_update(xntbase_t *base, u_long period)
{
	return 0;
}

static inline int xntbase_switch(const char *name, u_long period, xntbase_t **basep)
{
	return period == XN_APERIODIC_TICK ? 0 : -ENODEV;
}

static inline void xntbase_start(xntbase_t *base)
{
}

static inline void xntbase_stop(xntbase_t *base)
{
}

static inline void xntbase_tick(xntbase_t *base)
{
	xntimer_tick_aperiodic();
}

#endif /* !CONFIG_XENO_OPT_TIMING_PERIODIC */

#ifdef __cplusplus
}
#endif

static inline void xntbase_mount(void)
{
	inith(&nktbase.link);
	appendq(&nktimebaseq, &nktbase.link);
}

static inline void xntbase_umount(void)
{
	removeq(&nktimebaseq, &nktbase.link);
}

#endif /* __KERNEL__ || __XENO_SIM__ */

#endif /* !_XENO_NUCLEUS_TIMEBASE_H */

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

#ifndef _COPPERPLATE_CLOCKOBJ_H
#define _COPPERPLATE_CLOCKOBJ_H

#include <pthread.h>
#include <time.h>
#include <xeno_config.h>
#include <copperplate/list.h>
#include <copperplate/panic.h>

typedef unsigned long long ticks_t;

typedef long long sticks_t;

/*
 * We define the Copperplate clock as a monotonic, non-adjustable
 * one. This means that delays and timeouts won't be affected when the
 * kernel host date is changed. The implementation provides support
 * for absolute dates internally, with a per-clock epoch value, so
 * that different emulators can have different system dates.
 */
#ifdef CLOCK_MONOTONIC_RAW
#define CLOCK_COPPERPLATE  CLOCK_MONOTONIC_RAW
#else
#define CLOCK_COPPERPLATE  CLOCK_MONOTONIC
#endif

struct clockobj {
	pthread_mutex_t lock;
	struct timespec epoch;
	struct timespec offset;
	struct timespec start;
#ifndef CONFIG_XENO_LORES_CLOCK_DISABLED
	unsigned int resolution;
	unsigned int frequency;
#endif
	const char *name;	/* __ref FIXME */
};

void timespec_sub(struct timespec *r,
		  const struct timespec *t1, const struct timespec *t2);

void timespec_add(struct timespec *r,
		  const struct timespec *t1, const struct timespec *t2);

#ifdef __cplusplus
extern "C" {
#endif

void clockobj_set_date(struct clockobj *clkobj,
		       ticks_t ticks, unsigned int resolution_ns);

void clockobj_get_date(struct clockobj *clkobj, ticks_t *pticks);

void clockobj_ticks_to_timespec(struct clockobj *clkobj,
				ticks_t ticks, struct timespec *ts);

void clockobj_ticks_to_timeout(struct clockobj *clkobj,
			       ticks_t ticks, struct timespec *ts);

void clockobj_caltime_to_timeout(struct clockobj *clkobj, const struct tm *tm,
				 unsigned long rticks, struct timespec *ts);

void clockobj_caltime_to_ticks(struct clockobj *clkobj, const struct tm *tm,
			       unsigned long rticks, ticks_t *pticks);

void clockobj_ticks_to_caltime(struct clockobj *clkobj,
			       ticks_t ticks,
			       struct tm *tm,
			       unsigned long *rticks);

int clockobj_set_resolution(struct clockobj *clkobj,
			    unsigned int resolution_ns);

int clockobj_init(struct clockobj *clkobj,
		  const char *name, unsigned int resolution_ns);

int clockobj_destroy(struct clockobj *clkobj);

#ifdef __cplusplus
}
#endif

#ifdef CONFIG_XENO_LORES_CLOCK_DISABLED

static inline
int __clockobj_set_resolution(struct clockobj *clkobj,
			      unsigned int resolution_ns)
{
	if (resolution_ns > 1) {
		warning("support for low resolution clock disabled");
		return -EINVAL;
	}

	return 0;
}

static inline
unsigned int clockobj_get_resolution(struct clockobj *clkobj)
{
	return 1;
}

static inline
unsigned int clockobj_get_frequency(struct clockobj *clkobj)
{
	return 1000000000;
}

static inline sticks_t clockobj_ns_to_ticks(struct clockobj *clkobj,
					    sticks_t ns)
{
	return ns;
}

static inline sticks_t clockobj_ticks_to_ns(struct clockobj *clkobj,
					    sticks_t ticks)
{
	return ticks;
}

#else /* !CONFIG_XENO_LORES_CLOCK_DISABLED */

static inline
int __clockobj_set_resolution(struct clockobj *clkobj,
			      unsigned int resolution_ns)
{
	clkobj->resolution = resolution_ns;
	clkobj->frequency = 1000000000 / resolution_ns;

	return 0;
}

static inline
unsigned int clockobj_get_resolution(struct clockobj *clkobj)
{
	return clkobj->resolution;
}

static inline
unsigned int clockobj_get_frequency(struct clockobj *clkobj)
{
	return clkobj->frequency;
}

sticks_t clockobj_ns_to_ticks(struct clockobj *clkobj,
			      sticks_t ns);

static inline sticks_t clockobj_ticks_to_ns(struct clockobj *clkobj,
					    sticks_t ticks)
{
	return ticks * clkobj->resolution;
}

#endif /* !CONFIG_XENO_LORES_CLOCK_DISABLED */

ticks_t clockobj_get_tsc(void);

#ifdef CONFIG_XENO_COBALT

#include <asm-generic/xenomai/timeconv.h>

static inline sticks_t clockobj_ns_to_tsc(sticks_t ns)
{
	return xnarch_ns_to_tsc(ns);
}

static inline sticks_t clockobj_tsc_to_ns(sticks_t tsc)
{
	return xnarch_tsc_to_ns(tsc);
}

#else /* CONFIG_XENO_MERCURY */

static inline sticks_t clockobj_ns_to_tsc(sticks_t ns)
{
	return ns;
}

static inline sticks_t clockobj_tsc_to_ns(sticks_t tsc)
{
	return tsc;
}

#endif /* CONFIG_XENO_MERCURY */

#endif /* _COPPERPLATE_CLOCKOBJ_H */

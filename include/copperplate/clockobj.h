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
#include <copperplate/list.h>

typedef unsigned long long ticks_t;

struct clockobj {
	pthread_mutex_t lock;
	struct timespec epoch;
	struct timespec offset;
	struct timespec start;
	unsigned int tick_freq;
	unsigned int resolution;
	const char *name;	/* __ref FIXME */
};

void ticks_to_timespec(struct clockobj *clkobj,
		       ticks_t ticks,
		       struct timespec *ts);

void timespec_sub(struct timespec *r,
		  const struct timespec *t1, const struct timespec *t2);

void timespec_add(struct timespec *r,
		  const struct timespec *t1, const struct timespec *t2);

#ifdef __cplusplus
extern "C" {
#endif

int clockobj_set_date(struct clockobj *clkobj,
		      ticks_t ticks, unsigned int resolution_ns);

int clockobj_get_date(struct clockobj *clkobj, ticks_t *pticks);

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

unsigned int clockobj_get_resolution(struct clockobj *clkobj);

int clockobj_set_resolution(struct clockobj *clkobj, unsigned int resolution_ns);

int clockobj_init(struct clockobj *clkobj,
		  const char *name, unsigned int resolution_ns);

int clockobj_destroy(struct clockobj *clkobj);

#ifdef __cplusplus
}
#endif

#endif /* _COPPERPLATE_CLOCKOBJ_H */

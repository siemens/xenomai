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

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <limits.h>
#include <time.h>
#include <string.h>
#include "copperplate/lock.h"
#include "copperplate/clockobj.h"

void ticks_to_timespec(struct clockobj *clkobj,
		       ticks_t ticks,
		       struct timespec *ts)
{
	ts->tv_sec = ticks / clkobj->tick_freq;
	ts->tv_nsec = (ticks - (ts->tv_sec * clkobj->tick_freq)) * clkobj->resolution;
}

void timespec_sub(struct timespec *r,
		  const struct timespec *t1, const struct timespec *t2)
{
	r->tv_sec = t1->tv_sec - t2->tv_sec;
	r->tv_nsec = t1->tv_nsec - t2->tv_nsec;
	if (r->tv_nsec < 0) {
		r->tv_sec--;
		r->tv_nsec += 1000000000;
	}
}

void timespec_add(struct timespec *r,
		  const struct timespec *t1, const struct timespec *t2)
{
	r->tv_sec = t1->tv_sec + t2->tv_sec;
	r->tv_nsec = t1->tv_nsec + t2->tv_nsec;
	if (r->tv_nsec >= 1000000000) {
		r->tv_sec++;
		r->tv_nsec -= 1000000000;
	}
}

void clockobj_ticks_to_timespec(struct clockobj *clkobj,
				ticks_t ticks, struct timespec *ts)
{
	read_lock_nocancel(&clkobj->lock);
	ticks_to_timespec(clkobj, ticks, ts);
	read_unlock(&clkobj->lock);
}

void clockobj_ticks_to_timeout(struct clockobj *clkobj,
			       ticks_t ticks, struct timespec *ts)
{
	struct timespec delta;

	read_lock_nocancel(&clkobj->lock);
	clock_gettime(CLOCK_REALTIME, ts);
	ticks_to_timespec(clkobj, ticks, &delta);
	read_unlock(&clkobj->lock);
	timespec_add(ts, ts, &delta);
}

static const int mdays[] = {
	31, 28, 31, 30, 31, 30,	31, 31, 30, 31, 30, 31
};

void clockobj_caltime_to_ticks(struct clockobj *clkobj, const struct tm *tm,
			       unsigned long rticks, ticks_t *pticks)
{
	ticks_t t = 0;
	int n;	/* Must be signed. */

	/*
	 * We expect tick counts to be based on the time(2) epoch,
	 * i.e. 00:00:00 UTC, January 1, 1970.
	 */
	for (n = 1970; n <  1900 + tm->tm_year; n++)
		t += ((n % 4) ? 365 : 366);

	if (!(tm->tm_year % 4) && tm->tm_mon >= 2)
		/* Add one day for leap year after February. */
		t++;

	for (n = tm->tm_mon - 1; n >= 0; n--)
		t += mdays[n];

	t += tm->tm_mday - 1;
	t *= 24;
	t += tm->tm_hour;
	t *= 60;
	t += tm->tm_min;
	t *= 60;
	t += tm->tm_sec;
	t *= clkobj->tick_freq;
	t += rticks;

	/* XXX: we currently don't care about DST. */

	*pticks = t;
}

#define SECBYMIN	60
#define SECBYHOUR	(SECBYMIN * 60)
#define SECBYDAY	(SECBYHOUR * 24)

void clockobj_ticks_to_caltime(struct clockobj *clkobj,
			       ticks_t ticks,
			       struct tm *tm,
			       unsigned long *rticks)
{
	unsigned long year, month, day, hour, min, sec;
	time_t nsecs;

	read_lock_nocancel(&clkobj->lock);
	nsecs = ticks / clkobj->tick_freq;
	*rticks = ticks % clkobj->tick_freq;
	read_unlock(&clkobj->lock);

	for (year = 1970;; year++) { /* Years since 1970. */
		int ysecs = ((year % 4) ? 365 : 366) * SECBYDAY;
		if (ysecs > nsecs)
			break;
		nsecs -= ysecs;
	}

	for (month = 0;; month++) {
		int secbymon = mdays[month] * SECBYDAY;
		if (month == 1 && (year % 4) == 0)
			/* Account for leap year on February. */
			secbymon += SECBYDAY;
		if (secbymon > nsecs)
			break;
		nsecs -= secbymon;
	}

	day = nsecs / SECBYDAY;
	nsecs -= (day * SECBYDAY);
	hour = (nsecs / SECBYHOUR);
	nsecs -= (hour * SECBYHOUR);
	min = (nsecs / SECBYMIN);
	nsecs -= (min * SECBYMIN);
	sec = nsecs;

	memset(tm, 0, sizeof(*tm));
	tm->tm_year = year - 1900;
	tm->tm_mon = month;
	tm->tm_mday = day + 1;
	tm->tm_hour = hour;
	tm->tm_min = min;
	tm->tm_sec = sec;
}

void clockobj_caltime_to_timeout(struct clockobj *clkobj, const struct tm *tm,
				 unsigned long rticks, struct timespec *ts)
{
	ticks_t ticks;

	read_lock_nocancel(&clkobj->lock);
	clockobj_caltime_to_ticks(clkobj, tm, rticks, &ticks);
	ticks_to_timespec(clkobj, ticks, ts);
	timespec_sub(ts, ts, &clkobj->offset);
	read_unlock(&clkobj->lock);
}

int clockobj_set_date(struct clockobj *clkobj,
		      ticks_t ticks, unsigned int resolution_ns)
{
	struct timespec now;

	read_lock_nocancel(&clkobj->lock);

	clock_gettime(CLOCK_REALTIME, &now);

	/* Change the resolution on-the-fly if given. */
	if (resolution_ns) {
		clkobj->resolution = resolution_ns;
		clkobj->tick_freq = 1000000000 / resolution_ns;
	}

	ticks_to_timespec(clkobj, ticks, &clkobj->epoch);
	clkobj->start = now;
	timespec_sub(&clkobj->offset, &clkobj->epoch, &now);

	read_unlock(&clkobj->lock);

	return 0;
}

int clockobj_get_date(struct clockobj *clkobj, ticks_t *pticks)
{
	struct timespec now, delta, sum;

	read_lock_nocancel(&clkobj->lock);

	clock_gettime(CLOCK_REALTIME, &now);

	/* Wall clock time elapsed since we set the date: */
	timespec_sub(&delta, &now, &clkobj->start);

	/* Emulation time = epoch + delta. */
	timespec_add(&sum, &clkobj->epoch, &delta);

	/* Convert the time value to ticks. */
	*pticks = (ticks_t)sum.tv_sec * clkobj->tick_freq
	  + (ticks_t)sum.tv_nsec / clkobj->resolution;

	read_unlock(&clkobj->lock);

	return 0;
}

unsigned int clockobj_get_resolution(struct clockobj *clkobj)
{
	return clkobj->resolution;
}

int clockobj_set_resolution(struct clockobj *clkobj, unsigned int resolution_ns)
{
	/* Changing the resolution implies resetting the epoch. */
	return clockobj_set_date(clkobj, 0, resolution_ns);
}

int clockobj_init(struct clockobj *clkobj,
		  const char *name, unsigned int resolution_ns)
{
	pthread_mutexattr_t mattr;

	if (resolution_ns == 0)
		return -EINVAL;

	memset(clkobj, 0, sizeof(*clkobj));
	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_PRIVATE);
	pthread_mutex_init(&clkobj->lock, &mattr);
	pthread_mutexattr_destroy(&mattr);
	clkobj->resolution = resolution_ns;
	clkobj->tick_freq = 1000000000 / resolution_ns;
	clock_gettime(CLOCK_REALTIME, &clkobj->start);
	timespec_sub(&clkobj->offset, &clkobj->epoch, &clkobj->start);
	clkobj->name = name;

	return 0;
}

int clockobj_destroy(struct clockobj *clkobj)
{
	pthread_mutex_destroy(&clkobj->lock);
	return 0;
}

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

static void ticks_to_timespec(struct clockobj *clkobj,
			      ticks_t ticks,
			      struct timespec *ts)
{
	unsigned int freq = clockobj_get_frequency(clkobj);

	ts->tv_sec = ticks / freq;
	ts->tv_nsec = ticks - (ts->tv_sec * freq);
	if (clockobj_get_resolution(clkobj) > 1)
		ts->tv_nsec *= clockobj_get_resolution(clkobj);
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
	clock_gettime(CLOCK_COPPERPLATE, ts);
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
	t *= clockobj_get_frequency(clkobj);
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
	unsigned int freq;
	time_t nsecs;

	read_lock_nocancel(&clkobj->lock);
	freq = clockobj_get_frequency(clkobj);
	nsecs = ticks / freq;
	*rticks = ticks % freq;
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

void clockobj_set_date(struct clockobj *clkobj,
		       ticks_t ticks, unsigned int resolution_ns)
{
	struct timespec now;

	read_lock_nocancel(&clkobj->lock);

	clock_gettime(CLOCK_COPPERPLATE, &now);

	/* Change the resolution on-the-fly if given. */
	if (resolution_ns)
		__clockobj_set_resolution(clkobj, resolution_ns);

	ticks_to_timespec(clkobj, ticks, &clkobj->epoch);
	clkobj->start = now;
	timespec_sub(&clkobj->offset, &clkobj->epoch, &now);

	read_unlock(&clkobj->lock);
}

int clockobj_set_resolution(struct clockobj *clkobj, unsigned int resolution_ns)
{
#ifdef CONFIG_XENO_LORES_CLOCK_DISABLED
	assert(resolution_ns == 1);
#else
	/* Changing the resolution implies resetting the epoch. */
	clockobj_set_date(clkobj, 0, resolution_ns);
#endif
	return 0;
}

#ifdef CONFIG_XENO_COBALT

#include <asm/xenomai/arith.h>

ticks_t clockobj_get_tsc(void)
{
	/* Guaranteed to be the source of CLOCK_COPPERPLATE. */
	return __xn_rdtsc();
}

sticks_t clockobj_ns_to_ticks(struct clockobj *clkobj, sticks_t ns)
{
	/* Cobalt has optimized arith ops, use them. */
	return xnarch_ulldiv(ns, clkobj->resolution, NULL);
}

void clockobj_get_date(struct clockobj *clkobj,
		       ticks_t *pticks, ticks_t *ptsc)
{
	unsigned long long ns, tsc;

	read_lock_nocancel(&clkobj->lock);

	tsc = __xn_rdtsc();
	ns = xnarch_tsc_to_ns(tsc);
	ns += clkobj->offset.tv_sec * 1000000000;
	ns += clkobj->offset.tv_nsec;
	if (clockobj_get_resolution(clkobj) > 1)
		ns /= clockobj_get_resolution(clkobj);
	*pticks = ns;

	if (ptsc)
		*ptsc = tsc;

	read_unlock(&clkobj->lock);
}

#else /* CONFIG_XENO_MERCURY */

ticks_t clockobj_get_tsc(void)
{
	struct timespec now;
	clock_gettime(CLOCK_COPPERPLATE, &now);
	return now.tv_sec * 1000000000ULL + now.tv_nsec;
}

sticks_t clockobj_ns_to_ticks(struct clockobj *clkobj, sticks_t ns)
{
	return ns / clkobj->resolution;
}

void clockobj_get_date(struct clockobj *clkobj, ticks_t *pticks,
		       ticks_t *ptsc)
{
	struct timespec now, date;

	read_lock_nocancel(&clkobj->lock);

	clock_gettime(CLOCK_COPPERPLATE, &now);

	timespec_add(&date, &clkobj->offset, &now);

	/* Convert the time value to ticks. */
	*pticks = date.tv_sec * clockobj_get_frequency(clkobj)
		+ date.tv_nsec / clockobj_get_resolution(clkobj);

	/*
	 * Mercury has a single time source, with TSC == monotonic
	 * time.
	 */
	if (ptsc)
		*ptsc = *pticks;

	read_unlock(&clkobj->lock);
}

#endif /* CONFIG_XENO_MERCURY */

int clockobj_init(struct clockobj *clkobj,
		  const char *name, unsigned int resolution_ns)
{
	pthread_mutexattr_t mattr;
	int ret;

	if (resolution_ns == 0)
		return -EINVAL;

	memset(clkobj, 0, sizeof(*clkobj));
	ret = __clockobj_set_resolution(clkobj, resolution_ns);
	if (ret)
		return ret;

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_PRIVATE);
	pthread_mutex_init(&clkobj->lock, &mattr);
	pthread_mutexattr_destroy(&mattr);
	clock_gettime(CLOCK_COPPERPLATE, &clkobj->start);
	timespec_sub(&clkobj->offset, &clkobj->epoch, &clkobj->start);
	clkobj->name = name;

	return 0;
}

int clockobj_destroy(struct clockobj *clkobj)
{
	pthread_mutex_destroy(&clkobj->lock);
	return 0;
}

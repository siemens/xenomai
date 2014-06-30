/*
 * Copyright (C) 2014 Gilles Chanteperdrix <gch@xenomai.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#undef NDEBUG
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <unistd.h>
#include <time.h>
#include <sys/timerfd.h>
#include "check.h"

static void clock_increase_before_oneshot_timer_first_tick(void)
{
	unsigned long long ticks;
	struct itimerspec timer;
	struct timespec now;
	int t;

	check_unix(t = timerfd_create(CLOCK_REALTIME, 0));
	check_unix(clock_gettime(CLOCK_REALTIME, &now));
	timer.it_value = now;
	timer.it_value.tv_sec++;
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_nsec = 0;
	check_unix(timerfd_settime(t, TFD_TIMER_ABSTIME, &timer, NULL));
	now.tv_sec += 5;
	check_unix(clock_settime(CLOCK_REALTIME, &now));
	check_unix(clock_gettime(CLOCK_MONOTONIC, &now));
	check_unix(read(t, &ticks, sizeof(ticks)));
	assert(ticks == 1);
	timer.it_value = now;
	check_unix(clock_gettime(CLOCK_MONOTONIC, &now));
	assert(now.tv_sec * 1000000000ULL + now.tv_nsec -
		(timer.it_value.tv_sec * 1000000000ULL + timer.it_value.tv_nsec)
		< 1000000000);
	check_unix(close(t));
}

static void clock_increase_before_periodic_timer_first_tick(void)
{
	unsigned long long ticks;
	struct itimerspec timer;
	struct timespec now;
	int t;

	check_unix(t = timerfd_create(CLOCK_REALTIME, 0));
	check_unix(clock_gettime(CLOCK_REALTIME, &now));
	timer.it_value = now;
	timer.it_value.tv_sec++;
	timer.it_interval.tv_sec = 1;
	timer.it_interval.tv_nsec = 0;
	check_unix(timerfd_settime(t, TFD_TIMER_ABSTIME, &timer, NULL));
	now.tv_sec += 5;
	check_unix(clock_settime(CLOCK_REALTIME, &now));
	check_unix(clock_gettime(CLOCK_MONOTONIC, &now));
	check_unix(read(t, &ticks, sizeof(ticks)));
	assert(ticks == 5);
	timer.it_value = now;
	check_unix(clock_gettime(CLOCK_MONOTONIC, &now));
	assert(now.tv_sec * 1000000000ULL + now.tv_nsec -
		(timer.it_value.tv_sec * 1000000000ULL + timer.it_value.tv_nsec)
		< 1000000000);
	check_unix(read(t, &ticks, sizeof(ticks)));
	assert(ticks == 1);
	check_unix(close(t));
}

static void clock_increase_after_periodic_timer_first_tick(void)
{
	unsigned long long ticks;
	struct itimerspec timer;
	struct timespec now;
	int t;

	check_unix(t = timerfd_create(CLOCK_REALTIME, 0));
	check_unix(clock_gettime(CLOCK_REALTIME, &now));
	timer.it_value = now;
	timer.it_value.tv_sec++;
	timer.it_interval.tv_sec = 1;
	timer.it_interval.tv_nsec = 0;
	check_unix(timerfd_settime(t, TFD_TIMER_ABSTIME, &timer, NULL));
	check_unix(read(t, &ticks, sizeof(ticks)));
	assert(ticks == 1);
	check_unix(clock_gettime(CLOCK_REALTIME, &now));
	now.tv_sec += 5;
	check_unix(clock_settime(CLOCK_REALTIME, &now));
	check_unix(clock_gettime(CLOCK_MONOTONIC, &now));
	check_unix(read(t, &ticks, sizeof(ticks)));
	assert(ticks == 5);
	timer.it_value = now;
	check_unix(clock_gettime(CLOCK_MONOTONIC, &now));
	assert(now.tv_sec * 1000000000ULL + now.tv_nsec -
		(timer.it_value.tv_sec * 1000000000ULL + timer.it_value.tv_nsec)
		< 1000000000);
	check_unix(read(t, &ticks, sizeof(ticks)));
	assert(ticks == 1);
	check_unix(close(t));
}

static void clock_decrease_before_oneshot_timer_first_tick(void)
{
	unsigned long long ticks;
	struct itimerspec timer;
	struct timespec now;
	long long diff;
	int t;

	check_unix(t = timerfd_create(CLOCK_REALTIME, 0));
	check_unix(clock_gettime(CLOCK_REALTIME, &now));
	timer.it_value = now;
	timer.it_value.tv_sec++;
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_nsec = 0;
	check_unix(timerfd_settime(t, TFD_TIMER_ABSTIME, &timer, NULL));
	now.tv_sec -= 5;
	check_unix(clock_settime(CLOCK_REALTIME, &now));
	check_unix(clock_gettime(CLOCK_MONOTONIC, &now));
	check_unix(read(t, &ticks, sizeof(ticks)));
	assert(ticks == 1);
	timer.it_value = now;
	check_unix(clock_gettime(CLOCK_MONOTONIC, &now));
	diff = now.tv_sec * 1000000000ULL + now.tv_nsec -
		(timer.it_value.tv_sec * 1000000000ULL + timer.it_value.tv_nsec);
	assert(diff >= 5500000000LL && diff <= 6500000000LL);
	check_unix(close(t));
}

static void clock_decrease_before_periodic_timer_first_tick(void)
{
	unsigned long long ticks;
	struct itimerspec timer;
	struct timespec now;
	long long diff;
	int t;

	check_unix(t = timerfd_create(CLOCK_REALTIME, 0));
	check_unix(clock_gettime(CLOCK_REALTIME, &now));
	timer.it_value = now;
	timer.it_value.tv_sec++;
	timer.it_interval.tv_sec = 1;
	timer.it_interval.tv_nsec = 0;
	check_unix(timerfd_settime(t, TFD_TIMER_ABSTIME, &timer, NULL));
	now.tv_sec -= 5;
	check_unix(clock_settime(CLOCK_REALTIME, &now));
	check_unix(clock_gettime(CLOCK_MONOTONIC, &now));
	check_unix(read(t, &ticks, sizeof(ticks)));
	assert(ticks == 1);
	timer.it_value = now;
	check_unix(clock_gettime(CLOCK_MONOTONIC, &now));
	diff = now.tv_sec * 1000000000ULL + now.tv_nsec -
		(timer.it_value.tv_sec * 1000000000ULL + timer.it_value.tv_nsec);
	assert(diff >= 5500000000LL && diff <= 6500000000LL);
	check_unix(read(t, &ticks, sizeof(ticks)));
	assert(ticks == 1);
	check_unix(close(t));
}

static void clock_decrease_after_periodic_timer_first_tick(void)
{
	unsigned long long ticks;
	struct itimerspec timer;
	struct timespec now;
	long long diff;
	int t;

	check_unix(t = timerfd_create(CLOCK_REALTIME, 0));
	check_unix(clock_gettime(CLOCK_REALTIME, &now));
	timer.it_value = now;
	timer.it_value.tv_sec++;
	timer.it_interval.tv_sec = 1;
	timer.it_interval.tv_nsec = 0;
	check_unix(timerfd_settime(t, TFD_TIMER_ABSTIME, &timer, NULL));
	check_unix(read(t, &ticks, sizeof(ticks)));
	assert(ticks == 1);
	check_unix(clock_gettime(CLOCK_REALTIME, &now));
	now.tv_sec -= 5;
	check_unix(clock_settime(CLOCK_REALTIME, &now));
	check_unix(clock_gettime(CLOCK_MONOTONIC, &now));
	check_unix(read(t, &ticks, sizeof(ticks)));
	assert(ticks == 1);
	timer.it_value = now;
	check_unix(clock_gettime(CLOCK_MONOTONIC, &now));
	diff = now.tv_sec * 1000000000ULL + now.tv_nsec -
		(timer.it_value.tv_sec * 1000000000ULL + timer.it_value.tv_nsec);
	assert(diff < 1000000000);
	check_unix(read(t, &ticks, sizeof(ticks)));
	assert(ticks == 1);
	check_unix(close(t));
}

int main(void)
{
	clock_increase_before_oneshot_timer_first_tick();
	clock_increase_before_periodic_timer_first_tick();
	clock_increase_after_periodic_timer_first_tick();
	clock_decrease_before_oneshot_timer_first_tick();
	clock_decrease_before_periodic_timer_first_tick();
	clock_decrease_after_periodic_timer_first_tick();
	return EXIT_SUCCESS;
}

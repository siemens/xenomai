/*
 * Copyright (C) 2013 Gilles Chanteperdrix <gch@xenomai.org>
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
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/timerfd.h>
#include "check.h"

#ifndef TFD_NONBLOCK
#define TFD_NONBLOCK O_NONBLOCK
#endif

static void timerfd_basic_check(void)
{
	struct itimerspec its;
	int fd, i;
	
	check_unix(fd = timerfd_create(CLOCK_MONOTONIC, 0));

	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = 100000000;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 100000000;

	check_unix(timerfd_settime(fd, 0, &its, NULL));
	
	for (i = 0; i < 10; i++) {
		unsigned long long ticks;
		
		assert(check_unix(read(fd, &ticks, sizeof(ticks))) == 8);
		fprintf(stderr, "%Ld direct read ticks\n", ticks);
		assert(ticks >= 1);
	}
	
	close(fd);
}

static void timerfd_select_check(void)
{
	unsigned long long ticks;
	struct itimerspec its;
	fd_set inset;
	int fd, i;
	
	check_unix(fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK));

	FD_ZERO(&inset);
	FD_SET(fd, &inset);

	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = 100000000;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 100000000;

	check_unix(timerfd_settime(fd, 0, &its, NULL));
	assert(read(fd, &ticks, sizeof(ticks)) == -1 && errno == EAGAIN);
	
	for (i = 0; i < 10; i++) {
		fd_set tmp_inset = inset;

		check_unix(select(fd + 1, &tmp_inset, NULL, NULL, NULL));
		
		assert(check_unix(read(fd, &ticks, sizeof(ticks))) == 8);
		fprintf(stderr, "%Ld select+read ticks\n", ticks);
		assert(ticks >= 1);
	}
	
	close(fd);
}

static void timerfd_basic_overruns_check(void)
{
	struct itimerspec its;
	int fd, i;
	
	check_unix(fd = timerfd_create(CLOCK_MONOTONIC, 0));

	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = 100000000;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 100000000;

	check_unix(timerfd_settime(fd, 0, &its, NULL));
	
	for (i = 0; i < 3; i++) {
		unsigned long long ticks;
		
		sleep(1);
		assert(check_unix(read(fd, &ticks, sizeof(ticks))) == 8);
		fprintf(stderr, "%Ld direct read ticks\n", ticks);
		assert(ticks >= 10);
	}
	
	close(fd);
}

static void timerfd_select_overruns_check(void)
{
	unsigned long long ticks;
	struct itimerspec its;
	fd_set inset;
	int fd, i;
	
	check_unix(fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK));

	FD_ZERO(&inset);
	FD_SET(fd, &inset);

	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = 100000000;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 100000000;

	check_unix(timerfd_settime(fd, 0, &its, NULL));
	assert(read(fd, &ticks, sizeof(ticks)) == -1 && errno == EAGAIN);
	
	for (i = 0; i < 3; i++) {
		fd_set tmp_inset = inset;

		sleep(1);
		check_unix(select(fd + 1, &tmp_inset, NULL, NULL, NULL));
		
		assert(check_unix(read(fd, &ticks, sizeof(ticks))) == 8);
		fprintf(stderr, "%Ld select+read ticks\n", ticks);
		assert(ticks >= 10);
	}
	
	close(fd);
}

static void timerfd_select_overruns2_check(void)
{
	unsigned long long ticks;
	struct itimerspec its;
	fd_set inset;
	int fd, i;
	
	check_unix(fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK));

	FD_ZERO(&inset);
	FD_SET(fd, &inset);

	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = 100000000;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 100000000;

	check_unix(timerfd_settime(fd, 0, &its, NULL));
	assert(read(fd, &ticks, sizeof(ticks)) == -1 && errno == EAGAIN);
	
	for (i = 0; i < 3; i++) {
		fd_set tmp_inset = inset;

		check_unix(select(fd + 1, &tmp_inset, NULL, NULL, NULL));

		sleep(1);
		
		assert(check_unix(read(fd, &ticks, sizeof(ticks))) == 8);
		fprintf(stderr, "%Ld select+read ticks\n", ticks);
		assert(ticks >= 11);
	}
	
	close(fd);
}

static void timerfd_select_overruns_before_check(void)
{
	unsigned long long ticks;
	struct itimerspec its;
	fd_set inset;
	int fd, i;
	
	check_unix(fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK));

	FD_ZERO(&inset);
	FD_SET(fd, &inset);

	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = 100000000;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 100000000;

	check_unix(timerfd_settime(fd, 0, &its, NULL));
	assert(read(fd, &ticks, sizeof(ticks)) == -1 && errno == EAGAIN);

	sleep(1);

	for (i = 0; i < 3; i++) {
		fd_set tmp_inset = inset;

		check_unix(select(fd + 1, &tmp_inset, NULL, NULL, NULL));

		assert(check_unix(read(fd, &ticks, sizeof(ticks))) == 8);
		fprintf(stderr, "%Ld select+read ticks\n", ticks);
		assert(ticks >= 10);
		sleep(1);
	}
	
	close(fd);
}

static ssize_t
timed_read(int fd, void *buf, size_t len, struct timespec *ts)
{
	struct itimerspec its;
	ssize_t err;
	int tfd;
	
	check_unix(tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK));
	
	its.it_value = *ts;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 0;
	
	check_unix(timerfd_settime(tfd, TFD_WAKEUP, &its, NULL));
	
	err = read(fd, buf, len);
	if (err < 0)
		err = -errno;
	if (err == -EINTR) {
		unsigned long long ticks;
		
		err = read(tfd, &ticks, sizeof(ticks));
		if (err > 0)
			err = -ETIMEDOUT;
		else
			err = -EINTR;
	}
	
	check_unix(close(tfd));

	if (err >= 0)
		return err;
	
	errno = -err;
	return -1;
}

static void timerfd_unblock_check(void)
{
	unsigned long long ticks;
	struct itimerspec its;
	int fd;
	
	check_unix(fd = timerfd_create(CLOCK_MONOTONIC, 0));
	
	its.it_value.tv_sec = 5;
	its.it_value.tv_nsec = 0;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 0;
	
	check_unix(timerfd_settime(fd, 0, &its, NULL));

	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = 100000000;

	assert(timed_read(fd, &ticks, sizeof(ticks), &its.it_value) < 0 && 
		errno == ETIMEDOUT);

	check_unix(close(fd));
}


int main(void)
{
	timerfd_basic_check();
	timerfd_select_check();
	timerfd_basic_overruns_check();
	timerfd_select_overruns_check();
	timerfd_select_overruns2_check();
	timerfd_select_overruns_before_check();
	timerfd_unblock_check();

	return 0;
}

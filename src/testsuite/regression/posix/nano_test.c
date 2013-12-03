/*
 * Copyright (C) 2011-2013 Gilles Chanteperdrix <gch@xenomai.org>
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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <sched.h>
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/time.h>

#include "check.h"

static sig_atomic_t gotsig;

static void handle(int sig)
{
	gotsig = 1;
}

int main(void)
{
	struct timespec delay;
	struct sigaction sa;
	struct itimerval it;
	struct sched_param sp;
	int err;

	mlockall(MCL_CURRENT | MCL_FUTURE);

	sigemptyset(&sa.sa_mask);
	sa.sa_handler = handle;
	sa.sa_flags = 0;
	check_unix(sigaction(SIGALRM, &sa, NULL));

	sp.sched_priority = 1;
	check_pthread(pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp));

	it.it_interval.tv_sec = 1;
	it.it_interval.tv_usec = 0;
	it.it_value = it.it_interval;
	check_unix(setitimer(ITIMER_REAL, &it, NULL));

	delay.tv_sec = 5;
	delay.tv_nsec = 0;
	err = nanosleep(&delay, &delay);
	if (err != -1 || errno != EINTR || !gotsig || delay.tv_sec < 3 || delay.tv_sec > 4) {
		fprintf(stderr, "FAILURE, nanosleep: %s, received SIGALRM: %d, "
			"remaining time to sleep: %lu.%09lus\n",
			err ? strerror(errno) : strerror(err), gotsig,
			(unsigned long)delay.tv_sec, delay.tv_nsec);
		exit(EXIT_FAILURE);
	}

	fprintf(stderr, "Test OK\n");
	return EXIT_SUCCESS;
}

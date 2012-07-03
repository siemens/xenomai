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

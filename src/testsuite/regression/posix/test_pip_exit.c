/*
 * Test exiting a thread while holding a mutex with priority
 * inheritance enabled (and active).
 *
 * From a bug reported by Henri Roosen:
 * http://www.xenomai.org/pipermail/xenomai/2012-September/026073.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include <semaphore.h>
#include "check.h"

static pthread_mutex_t mutex;
static sem_t ready;

static void locker(void)
{
	check_pthread(pthread_set_name_np(pthread_self(), "locker"));
	check_pthread(pthread_mutex_lock(&mutex));
	check_unix(sem_post(&ready));
}

static void waiter(void)
{
	check_pthread(pthread_set_name_np(pthread_self(), "waiter"));
	check_unix(sem_wait(&ready));
	check_pthread(pthread_mutex_lock(&mutex));
}

static void *thread(void *cookie)
{
	locker();
	/* Now let the waiter enter pthread_mutex_lock and cause the
	   PIP boost */
	sleep(1);
	return cookie;
}

int main(void)
{
	pthread_mutexattr_t mattr;
	struct sched_param sp;
	pthread_t tid;

	check_unix(mlockall(MCL_CURRENT | MCL_FUTURE));

	check_pthread(pthread_mutexattr_init(&mattr));
	check_pthread(pthread_mutexattr_setprotocol(&mattr,
						    PTHREAD_PRIO_INHERIT));

	check_pthread(pthread_mutex_init(&mutex, &mattr));
	check_pthread(pthread_mutexattr_destroy(&mattr));

	check_unix(sem_init(&ready, 0, 0));

	check_pthread(pthread_create(&tid, NULL, thread, NULL));

	sp.sched_priority = 99;
	check_pthread(pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp));

	waiter();

	fprintf(stderr, "Test OK\n");
	exit(EXIT_SUCCESS);
}

#include <stdio.h>
#include <stdlib.h>

#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <mqueue.h>

#include <nucleus/heap.h>

#include "check.h"

#define SEM_NAME "/sem"
#define SHM_NAME "/shm"
#define SHM_SZ 16384
#define MQ_NAME "/mq"

#define check_used(object, before, failed)				\
	({								\
		unsigned long long after = get_used();			\
		if (before != after) {					\
			fprintf(stderr, object		\
				" leaked %Lu bytes\n", after-before);	\
			failed = 1;					\
		} else							\
			fprintf(stderr, object ": OK\n");		\
	})

unsigned long long get_used(void)
{
	unsigned long long used = 0;
	struct xnheap_desc hd;
	int i;

	for (i = 0; XENOMAI_SYSCALL2(__xn_sys_heap_info, &hd, i) == 0; i++)
		used += hd.used;

	if (used == 0) {
		fprintf(stderr, "Error: could not get size of used memory\n");
		exit(EXIT_FAILURE);
	}

	return used;
}

void *empty(void *cookie)
{
	return cookie;
}

int main(void)
{
	unsigned long long before;
	struct sigevent sevt;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	int fd, failed = 0;
	pthread_t thread;
	sem_t sem, *psem;
	timer_t tm;
	void *shm;

	mlockall(MCL_CURRENT|MCL_FUTURE);

	fprintf(stderr, "Checking for leaks in posix skin objects\n");
	before = get_used();
	check_pthread(pthread_create(&thread, NULL, empty, NULL));
	check_pthread(pthread_join(thread, NULL));
	sleep(1);		/* Leave some time for xnheap
				 * deferred free */
	check_used("thread", before, failed);

	before = get_used();
	check_pthread(pthread_mutex_init(&mutex, NULL));
	check_pthread(pthread_mutex_destroy(&mutex));
	check_used("mutex", before, failed);

	before = get_used();
	check_pthread(pthread_cond_init(&cond, NULL));
	check_pthread(pthread_cond_destroy(&cond));
	check_used("cond", before, failed);

	before = get_used();
	check_unix(sem_init(&sem, 0, 0));
	check_unix(sem_destroy(&sem));
	check_used("sem", before, failed);

	before = get_used();
	check_unix(-!(psem = sem_open(SEM_NAME, O_CREAT, 0644, 1)));
	check_unix(sem_close(psem));
	check_unix(sem_unlink(SEM_NAME));
	check_used("named sem", before, failed);

	before = get_used();
	sevt.sigev_notify = SIGEV_SIGNAL;
	sevt.sigev_signo = SIGALRM;
	check_unix(timer_create(CLOCK_MONOTONIC, &sevt, &tm));
	check_unix(timer_delete(tm));
	check_used("timer", before, failed);

	before = get_used();
	check_unix(fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0644));
	check_unix(ftruncate(fd, SHM_SZ));
	shm = mmap(NULL, SHM_SZ, PROT_READ, MAP_SHARED, fd, 0);
	check_unix(shm == MAP_FAILED ? -1 : 0);
	check_unix(munmap(shm, SHM_SZ));
	check_unix(close(fd));
	check_unix(shm_unlink(SHM_NAME));
	check_used("shm", before, failed);

	before = get_used();
	check_unix(fd = mq_open(MQ_NAME, O_RDWR | O_CREAT, 0644, NULL));
	check_unix(mq_close(fd));
	check_unix(mq_unlink(MQ_NAME));
	check_used("mq", before, failed);

	return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

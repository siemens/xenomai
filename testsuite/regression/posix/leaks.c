#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <mqueue.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <linux/unistd.h>
#include <boilerplate/compiler.h>
#include <rtdm/rtdm.h>
#include <cobalt/uapi/kernel/heap.h>
#include <asm/xenomai/syscall.h>

#include "check.h"

#define SEM_NAME "/sem"
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

#define devnode_root "/dev/rtdm/"

static unsigned long long get_used(void)
{
	const char *memdev[] = {
		devnode_root COBALT_MEMDEV_PRIVATE,
		devnode_root COBALT_MEMDEV_SHARED,
		devnode_root COBALT_MEMDEV_SYS,
		NULL,
	};
	struct cobalt_memdev_stat statbuf;
	unsigned long long used = 0;
	int i, fd;

	for (i = 0; memdev[i]; i++) {
		fd = open(memdev[i], O_RDONLY);
		if (fd < 0 || ioctl(fd, MEMDEV_RTIOC_STAT, &statbuf)) {
			fprintf(stderr, "Error: could not open/ioctl device %s\n",
				memdev[i]);
			exit(EXIT_FAILURE);
		}
		used += statbuf.size - statbuf.free;
	}

	return used;
}

static void *empty(void *cookie)
{
	return cookie;
}

static inline void subprocess_leak(void)
{
	struct sigevent sevt;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	pthread_t thread;
	sem_t sem, *psem;
	timer_t tm;
	int fd;

	check_pthread(pthread_create(&thread, NULL, empty, NULL));
	check_pthread(pthread_mutex_init(&mutex, NULL));
	check_pthread(pthread_cond_init(&cond, NULL));
	check_unix(sem_init(&sem, 0, 0));
	check_unix(-!(psem = sem_open(SEM_NAME, O_CREAT, 0644, 1)));
	sevt.sigev_notify = SIGEV_THREAD_ID;
	sevt.sigev_signo = SIGALRM;
	sevt.sigev_notify_thread_id = syscall(__NR_gettid);
	check_unix(timer_create(CLOCK_MONOTONIC, &sevt, &tm));
	check_unix(fd = mq_open(MQ_NAME, O_RDWR | O_CREAT, 0644, NULL));
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
	__maybe_unused pid_t child;

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
	sevt.sigev_notify = SIGEV_THREAD_ID;
	sevt.sigev_signo = SIGALRM;
	sevt.sigev_notify_thread_id = syscall(__NR_gettid);
	check_unix(timer_create(CLOCK_MONOTONIC, &sevt, &tm));
	check_unix(timer_delete(tm));
	check_used("timer", before, failed);

	before = get_used();
	check_unix(fd = mq_open(MQ_NAME, O_RDWR | O_CREAT, 0644, NULL));
	check_unix(mq_close(fd));
	check_unix(mq_unlink(MQ_NAME));
	check_used("mq", before, failed);

#ifdef HAVE_FORK
	before = get_used();
	check_unix(child = fork());
	if (!child) {
		subprocess_leak();
		return EXIT_SUCCESS;
	}
	while (waitpid(child, NULL, 0) == -1 && errno == EINTR);
	sleep(1);		/* Leave some time for xnheap
				 * deferred free */
	check_unix(sem_unlink(SEM_NAME));
	check_unix(mq_unlink(MQ_NAME));
	check_used("fork", before, failed);
#endif

	return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

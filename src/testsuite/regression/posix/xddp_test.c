/*
 * XDDP-based RT/NRT threads regression test.
 *
 * Original author: Doug Brunner
 *
 * This test causes a crash with Xenomai 2.6.1 and earlier versions.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <pthread.h>
#include <semaphore.h>

#include <rtdm/rtipc.h>
#include "check.h"

static pthread_t rt, nrt;
static sem_t opened;
static int xddp_port = -1;	/* First pass uses auto-selection */

static void *realtime_thread(void *arg)
{
	unsigned long count = (unsigned long)arg;
	struct sockaddr_ipc saddr;
	struct timespec ts;
	socklen_t addrlen;
	size_t poolsz;
	int ret, s;

	/*
	 * Get a datagram socket to bind to the RT endpoint. Each
	 * endpoint is represented by a port number within the XDDP
	 * protocol namespace.
	 */
	s = check_unix(socket(AF_RTIPC, SOCK_DGRAM, IPCPROTO_XDDP));

	/*
	 * Set a local 16k pool for the RT endpoint. Memory needed to
	 * convey datagrams will be pulled from this pool, instead of
	 * Xenomai's system pool.
	 */
	poolsz = 16384; /* bytes */
	ret = check_unix(setsockopt(s, SOL_XDDP, XDDP_POOLSZ,
				    &poolsz, sizeof(poolsz)));

	/*
	 * Bind the socket to the port, to setup a proxy to channel
	 * traffic to/from the Linux domain.
	 *
	 * saddr.sipc_port specifies the port number to use.
	 */
	memset(&saddr, 0, sizeof(saddr));
	saddr.sipc_family = AF_RTIPC;
	saddr.sipc_port = xddp_port;
	ret = bind(s, (struct sockaddr *)&saddr, sizeof(saddr));
	if (count == 1) {
		if (ret < 0 && errno == EADDRINUSE) {
			fprintf(stderr, "Test OK\n");
			exit(EXIT_SUCCESS);
		}
		if (ret < 0) {
			fprintf(stderr, "FAILURE: bind: %m\n");
			exit(EXIT_FAILURE);
		}
		fprintf(stderr, "FAILURE: bind returned %d\n", ret);
	} else {
		addrlen = sizeof(saddr);
		check_unix(getsockname(s, (struct sockaddr *)&saddr, &addrlen));
		xddp_port = saddr.sipc_port;
	}
	if (ret < 0) {
		fprintf(stderr, "FAILURE bind: %m\n");
		exit(EXIT_FAILURE);
	}

	check_unix(sem_post(&opened));
	ts.tv_sec = 0;
	ts.tv_nsec = 500000000; /* 500 ms */
	check_unix(clock_nanosleep(CLOCK_REALTIME, 0, &ts, NULL));
	check_unix(sem_wait(&opened));
	check_unix(sem_destroy(&opened));
	check_unix(close(s));

	return NULL;
}

static void *regular_thread(void *arg)
{
	char buf[128], *devname;
	int fd;

	check_unix(asprintf(&devname, "/dev/rtp%d", xddp_port));

	fd = check_unix(open(devname, O_RDWR));
	free(devname);
	check_unix(sem_post(&opened));

	for (;;) {
		/* Get the next message from realtime_thread. */
		read(fd, buf, sizeof(buf));

		usleep(10000);
	}

	return NULL;
}

static void cleanup_upon_sig(int sig)
{
	pthread_cancel(rt);
	pthread_cancel(nrt);
	signal(sig, SIG_DFL);
	pthread_join(rt, NULL);
	pthread_join(nrt, NULL);
	raise(sig);
}

int main(int argc, char **argv)
{
	struct sched_param rtparam = { .sched_priority = 42 };
	pthread_attr_t rtattr, regattr;
	sigset_t mask, oldmask;

	check_unix(mlockall(MCL_CURRENT | MCL_FUTURE));

	check_unix(sigemptyset(&mask));
	check_unix(sigaddset(&mask, SIGINT));
	check_unix(signal(SIGINT, cleanup_upon_sig) == SIG_ERR ? -1 : 0);
	check_unix(sigaddset(&mask, SIGTERM));
	check_unix(signal(SIGTERM, cleanup_upon_sig) == SIG_ERR ? -1 : 0);
	check_unix(sigaddset(&mask, SIGHUP));
	check_unix(signal(SIGHUP, cleanup_upon_sig) == SIG_ERR ? -1 : 0);
	check_pthread(pthread_sigmask(SIG_BLOCK, &mask, &oldmask));

	check_unix(sem_init(&opened, 0, 0));
	check_pthread(pthread_attr_init(&rtattr));
	check_pthread(pthread_attr_setdetachstate(&rtattr,
						  PTHREAD_CREATE_JOINABLE));
	check_pthread(pthread_attr_setinheritsched(&rtattr,
						   PTHREAD_EXPLICIT_SCHED));
	check_pthread(pthread_attr_setschedpolicy(&rtattr, SCHED_FIFO));
	check_pthread(pthread_attr_setschedparam(&rtattr, &rtparam));

	check_pthread(pthread_create(&rt, &rtattr, &realtime_thread, NULL));
	check_pthread(pthread_attr_destroy(&rtattr));
	check_unix(sem_wait(&opened));

	check_pthread(pthread_attr_init(&regattr));
	check_pthread(pthread_attr_setdetachstate(&regattr,
						  PTHREAD_CREATE_JOINABLE));
	check_pthread(pthread_attr_setinheritsched(&regattr,
						   PTHREAD_EXPLICIT_SCHED));
	check_pthread(pthread_attr_setschedpolicy(&regattr, SCHED_OTHER));

	check_pthread(pthread_create(&nrt, &regattr, &regular_thread, NULL));
	check_pthread(pthread_attr_destroy(&regattr));

	/* after this call returns the RT thread will have ended */
	sleep(1);

	/* start another RT thread to cause the crash */
	check_pthread(pthread_attr_init(&rtattr));
	check_pthread(pthread_attr_setdetachstate(&rtattr,
						  PTHREAD_CREATE_JOINABLE));
	check_pthread(pthread_attr_setinheritsched(&rtattr,
						   PTHREAD_EXPLICIT_SCHED));
	check_pthread(pthread_attr_setschedpolicy(&rtattr, SCHED_FIFO));
	check_pthread(pthread_attr_setschedparam(&rtattr, &rtparam));

	check_pthread(pthread_create(&rt, &rtattr,
				     &realtime_thread, (void *)1UL));
	check_pthread(pthread_attr_destroy(&rtattr));

	check_unix(sigsuspend(&oldmask));

	return 0;
}

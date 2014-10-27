/*
 * SCHED_TP setup test.
 *
 * Copyright (C) Philippe Gerum <rpm@xenomai.org>
 *
 * Released under the terms of GPLv2.
 */
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <memory.h>
#include <malloc.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>
#include <errno.h>
#include <error.h>
#include <smokey/smokey.h>

smokey_test_plugin(sched_tp,
		   SMOKEY_NOARGS,
		   "Check the SCHED_TP scheduling policy"
);

int clock_nanosleep(clockid_t __clock_id, int __flags,
		    __const struct timespec *__req,
		    struct timespec *__rem);

static pthread_t threadA, threadB, threadC;

static sem_t barrier;

static void *thread_body(void *arg)
{
	pthread_t me = pthread_self();
	struct sched_param_ex param;
	struct timespec ts;
	cpu_set_t affinity;
	int ret, part;

	CPU_ZERO(&affinity);
	CPU_SET(0, &affinity);
	ret = sched_setaffinity(0, sizeof(affinity), &affinity);
	if (ret)
		error(1, errno, "sched_setaffinity");

	part = (int)(long)arg;
	param.sched_priority = 50 - part;
	param.sched_tp_partition = part;
	ret = pthread_setschedparam_ex(me, SCHED_TP, &param);
	if (ret)
		error(1, ret, "pthread_setschedparam_ex");

	sem_wait(&barrier);
	sem_post(&barrier);

	for (;;) {
		putchar('A' + part);
		fflush(stdout);
		ts.tv_sec = 0;
		ts.tv_nsec = 10000000;
		clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
	}

	return NULL;
}

static void cleanup(void)
{
	pthread_cancel(threadC);
	pthread_cancel(threadB);
	pthread_cancel(threadA);
	pthread_join(threadC, NULL);
	pthread_join(threadB, NULL);
	pthread_join(threadA, NULL);
}

static void __create_thread(pthread_t *tid, const char *name, int seq)
{
	struct sched_param param = { .sched_priority = 1 };
	pthread_attr_t attr;
	int ret;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
	pthread_attr_setschedparam(&attr, &param);
	pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN * 2);
	ret = pthread_create(tid, &attr, thread_body, (void *)(long)seq);
	if (ret)
		error(1, ret, "pthread_create");

	pthread_attr_destroy(&attr);
	pthread_setname_np(*tid, name);
}

#define create_thread(tid, n) __create_thread(&(tid), # tid, n)
#define NR_WINDOWS  4

static int run_sched_tp(struct smokey_test *t, int argc, char *const argv[])
{
	union sched_config *p;
	size_t len;
	int ret, n;

	/*
	 * For a recurring global time frame of 400 ms, we define a TP
	 * schedule as follows:
	 *
	 * - thread(s) assigned to partition #2 (tag C) shall be
	 * allowed to run for 100 ms, when the next global time frame
	 * begins.
	 *
	 * - thread(s) assigned to partition #1 (tag B) shall be
	 * allowed to run for 50 ms, after the previous time slot
	 * ends.
	 *
	 * - thread(s) assigned to partition #0 (tag A) shall be
	 * allowed to run for 20 ms, after the previous time slot
	 * ends.
	 *
	 * - when the previous time slot ends, no TP thread shall be
	 * allowed to run until the global time frame ends (special
	 * setting of ptid == -1), i.e. 230 ms.
	 */
	len = sched_tp_confsz(NR_WINDOWS);
	p = malloc(len);
	if (p == NULL)
		error(1, ENOMEM, "malloc");

	p->tp.nr_windows = NR_WINDOWS;
	p->tp.windows[0].offset.tv_sec = 0;
	p->tp.windows[0].offset.tv_nsec = 0;
	p->tp.windows[0].duration.tv_sec = 0;
	p->tp.windows[0].duration.tv_nsec = 100000000;
	p->tp.windows[0].ptid = 2;
	p->tp.windows[1].offset.tv_sec = 0;
	p->tp.windows[1].offset.tv_nsec = 100000000;
	p->tp.windows[1].duration.tv_sec = 0;
	p->tp.windows[1].duration.tv_nsec = 50000000;
	p->tp.windows[1].ptid = 1;
	p->tp.windows[2].offset.tv_sec = 0;
	p->tp.windows[2].offset.tv_nsec = 150000000;
	p->tp.windows[2].duration.tv_sec = 0;
	p->tp.windows[2].duration.tv_nsec = 20000000;
	p->tp.windows[2].ptid = 0;
	p->tp.windows[3].offset.tv_sec = 0;
	p->tp.windows[3].offset.tv_nsec = 170000000;
	p->tp.windows[3].duration.tv_sec = 0;
	p->tp.windows[3].duration.tv_nsec = 230000000;
	p->tp.windows[3].ptid = -1;

	/* Assign the TP schedule to CPU #0 */
	ret = sched_setconfig_np(0, SCHED_TP, p, len);
	if (ret)
		error(1, ret, "sched_setconfig_np");

	memset(p, 0xa5, len);

	ret = sched_getconfig_np(0, SCHED_TP, p, &len);
	if (ret)
		error(1, ret, "sched_getconfig_np");

	printf("check: %d windows\n", p->tp.nr_windows);
	for (n = 0; n < 4; n++)
		printf("[%d] offset = { %ld s, %ld ns }, duration = { %ld s, %ld ns }, ptid = %d\n",
		       n,
		       p->tp.windows[n].offset.tv_sec,
		       p->tp.windows[n].offset.tv_nsec,
		       p->tp.windows[n].duration.tv_sec,
		       p->tp.windows[n].duration.tv_nsec,
		       p->tp.windows[n].ptid);

	sem_init(&barrier, 0, 0);
	create_thread(threadA, 0);
	create_thread(threadB, 1);
	create_thread(threadC, 2);
	sem_post(&barrier);

	sleep(5);
	cleanup();
	sem_destroy(&barrier);

	return 0;
}

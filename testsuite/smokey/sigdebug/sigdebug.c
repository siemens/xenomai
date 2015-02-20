/*
 * Functional testing of unwanted domain switch debugging mechanism.
 *
 * Copyright (C) Siemens AG, 2012-2014
 *
 * Authors:
 *  Jan Kiszka  <jan.kiszka@siemens.com>
 *
 * Released under the terms of GPLv2.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <pthread.h>
#include <semaphore.h>
#include <asm/unistd.h>
#include <smokey/smokey.h>

smokey_test_plugin(sigdebug,
		   SMOKEY_NOARGS,
		   "Check SIGDEBUG reporting."
);

unsigned int expected_reason;
bool sigdebug_received;
pthread_mutex_t prio_invert;
sem_t send_signal;
char *mem;
FILE *wd;

static void setup_checkdebug(unsigned int reason)
{
	sigdebug_received = false;
	expected_reason = reason;
}

static void check_inner(const char *fn, int line, const char *msg,
			int status, int expected)
{
	if (status == expected)
		return;

	pthread_setmode_np(PTHREAD_WARNSW, 0, NULL);
	rt_print_flush_buffers();
	fprintf(stderr, "FAILURE %s:%d: %s returned %d instead of %d - %s\n",
		fn, line, msg, status, expected, strerror(-status));
	exit(EXIT_FAILURE);
}

static void check_sigdebug_inner(const char *fn, int line, const char *reason)
{
	if (sigdebug_received)
		return;

	pthread_setmode_np(PTHREAD_WARNSW, 0, NULL);
	rt_print_flush_buffers();
	fprintf(stderr, "FAILURE %s:%d: no %s received\n", fn, line, reason);
	exit(EXIT_FAILURE);
}

#define check(msg, status, expected) ({					\
	int __status = status;						\
	check_inner(__FUNCTION__, __LINE__, msg, __status, expected);	\
	__status;							\
})

#define check_no_error(msg, status) ({					\
	int __status = status;						\
	check_inner(__FUNCTION__, __LINE__, msg,			\
		    __status < 0 ? __status : 0, 0);			\
	__status;							\
})

#define check_sigdebug_received(reason) do {				\
	const char *__reason = reason;					\
	check_sigdebug_inner(__FUNCTION__, __LINE__, __reason);		\
} while (0)

static void *rt_thread_body(void *cookie)
{
	struct timespec now, delay = {.tv_sec = 0, .tv_nsec = 10000000LL};
	unsigned long long end;
	int err;

	err = pthread_setmode_np(0, PTHREAD_WARNSW, NULL);
	check_no_error("pthread_setmode_np", err);

	printf("syscall\n");
	setup_checkdebug(SIGDEBUG_MIGRATE_SYSCALL);
	syscall(__NR_gettid);
	check_sigdebug_received("SIGDEBUG_MIGRATE_SYSCALL");

	printf("signal\n");
	setup_checkdebug(SIGDEBUG_MIGRATE_SIGNAL);
	err = sem_post(&send_signal);
	check_no_error("sem_post", err);
	err = clock_nanosleep(CLOCK_MONOTONIC, 0, &delay, NULL);
	check_no_error("clock_nanosleep", err);
	check_sigdebug_received("SIGDEBUG_MIGRATE_SIGNAL");

	printf("relaxed mutex owner\n");
	setup_checkdebug(SIGDEBUG_MIGRATE_PRIOINV);
	err = pthread_mutex_lock(&prio_invert);
	check_no_error("pthread_mutex_lock", err);
	check_sigdebug_received("SIGDEBUG_MIGRATE_PRIOINV");

	printf("page fault\n");
	setup_checkdebug(SIGDEBUG_MIGRATE_FAULT);
	delay.tv_nsec = 0;
	err = clock_nanosleep(CLOCK_MONOTONIC, 0, &delay, NULL);
	check_no_error("clock_nanosleep", err);
	*mem ^= 0xFF;
	check_sigdebug_received("SIGDEBUG_MIGRATE_FAULT");

	if (wd) {
		printf("watchdog\n");
		rt_print_flush_buffers();
		setup_checkdebug(SIGDEBUG_WATCHDOG);
		clock_gettime(CLOCK_MONOTONIC, &now);
		end = now.tv_sec * 1000000000ULL + now.tv_nsec + 2100000000ULL;
		err = clock_nanosleep(CLOCK_MONOTONIC, 0, &delay, NULL);
		check_no_error("clock_nanosleep", err);
		do
			clock_gettime(CLOCK_MONOTONIC, &now);
		while (now.tv_sec * 1000000000ULL + now.tv_nsec < end &&
			 !sigdebug_received);
		check_sigdebug_received("SIGDEBUG_WATCHDOG");
	}

	printf("lock break\n");
	setup_checkdebug(SIGDEBUG_LOCK_BREAK);
	err = pthread_setmode_np(0, PTHREAD_LOCK_SCHED |
				    PTHREAD_DISABLE_LOCKBREAK, NULL);
	check_no_error("pthread_setmode_np", err);
	delay.tv_nsec = 1000000LL;
	err = clock_nanosleep(CLOCK_MONOTONIC, 0, &delay, NULL);
	check("clock_nanosleep", err, EINTR);
	check_sigdebug_received("SIGDEBUG_LOCK_BREAK");

	return NULL;
}

static void sigdebug_handler(int sig, siginfo_t *si, void *context)
{
	unsigned int reason = sigdebug_reason(si);

	if (reason != expected_reason) {
		rt_print_flush_buffers();
		fprintf(stderr, "FAILURE: sigdebug_handler expected reason %d,"
			" received %d\n", expected_reason, reason);
		exit(EXIT_FAILURE);
	}
	sigdebug_received = true;
}

static void dummy_handler(int sig, siginfo_t *si, void *context)
{
}

static int run_sigdebug(struct smokey_test *t, int argc, char *const argv[])
{
	char tempname[] = "/tmp/sigdebug-XXXXXX";
	char buf[BUFSIZ], dev[BUFSIZ];
	struct sched_param params = {.sched_priority = 1};
	pthread_t rt_thread;
	pthread_attr_t attr;
	pthread_mutexattr_t mutex_attr;
	struct timespec delay = {.tv_sec = 0, .tv_nsec = 20000000ULL};
	long int start, trash, end;
	unsigned char *mayday, *p;
	struct sigaction sa;
	int old_wd_value;
	char r, w, x, s;
	int tmp_fd, d;
	FILE *maps;
	int err;

	rt_print_auto_init(1);

	if (argc < 2 || strcmp(argv[1], "--skip-watchdog") != 0) {
		wd = fopen("/sys/module/xenomai/parameters/watchdog_timeout",
			   "w+");
		if (!wd) {
			fprintf(stderr, "FAILURE: no watchdog available and "
					"--skip-watchdog not specified\n");
			exit(EXIT_FAILURE);
		}
		err = fscanf(wd, "%d", &old_wd_value);
		check("get watchdog", err, 1);
		err = fprintf(wd, "2");
		check("set watchdog", err, 1);
		fflush(wd);
	}

	maps = fopen("/proc/self/maps", "r");
	if (maps == NULL) {
		perror("open /proc/self/maps");
		exit(EXIT_FAILURE);
	}

	while (fgets(buf, sizeof(buf), maps)) {
		if (sscanf(buf, "%lx-%lx %c%c%c%c %lx %x:%x %d%s\n",
			   &start, &end, &r, &w, &x, &s, &trash,
			   &d, &d, &d, dev) == 11
		    && r == 'r' && x == 'x'
		    && !strcmp(dev, "/dev/rtheap") && end - start == 4096) {
			printf("mayday page starting at 0x%lx [%s]\n"
			       "mayday code:", start, dev);
			mayday = (unsigned char *)start;
			for (p = mayday; p < mayday + 32; p++)
				printf(" %.2x", *p);
			printf("\n");
		}
	}
	fclose(maps);

	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = sigdebug_handler;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGDEBUG, &sa, NULL);

	sa.sa_sigaction = dummy_handler;
	sigaction(SIGUSR1, &sa, NULL);

	errno = 0;
	tmp_fd = mkstemp(tempname);
	check_no_error("mkstemp", -errno);
	unlink(tempname);
	check_no_error("unlink", -errno);
	mem = mmap(NULL, 1, PROT_READ | PROT_WRITE, MAP_SHARED, tmp_fd, 0);
	check_no_error("mmap", -errno);
	err = write(tmp_fd, "X", 1);
	check("write", err, 1);

	err = pthread_setschedparam(pthread_self(), SCHED_FIFO, &params);
	check_no_error("pthread_setschedparam", err);

	err = pthread_mutexattr_init(&mutex_attr);
	check_no_error("pthread_mutexattr_init", err);
	err = pthread_mutexattr_setprotocol(&mutex_attr, PTHREAD_PRIO_INHERIT);
	check_no_error("pthread_mutexattr_setprotocol", err);
	err = pthread_mutex_init(&prio_invert, &mutex_attr);
	check_no_error("pthread_mutex_init", err);

	err = pthread_mutex_lock(&prio_invert);
	check_no_error("pthread_mutex_lock", err);

	err = sem_init(&send_signal, 0, 0);
	check_no_error("sem_init", err);

	err = pthread_attr_init(&attr);
	check_no_error("pthread_attr_init", err);
	err = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	check_no_error("pthread_attr_setinheritsched", err);
	err = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
	check_no_error("pthread_attr_setschedpolicy", err);
	params.sched_priority = 2;
	err = pthread_attr_setschedparam(&attr, &params);
	check_no_error("pthread_attr_setschedparam", err);

	printf("mlockall\n");
	munlockall();
	setup_checkdebug(SIGDEBUG_NOMLOCK);
	err = pthread_create(&rt_thread, &attr, rt_thread_body, NULL);
	check("pthread_setschedparam", err, EINTR);
	check_sigdebug_received("SIGDEBUG_NOMLOCK");
	mlockall(MCL_CURRENT | MCL_FUTURE);

	err = pthread_create(&rt_thread, &attr, rt_thread_body, NULL);
	check_no_error("pthread_create", err);

	err = sem_wait(&send_signal);
	check_no_error("sem_wait", err);
	err = __real_pthread_kill(rt_thread, SIGUSR1);
	check_no_error("pthread_kill", err);

	__STD(nanosleep(&delay, NULL));

	err = pthread_mutex_unlock(&prio_invert);
	check_no_error("pthread_mutex_unlock", err);

	err = pthread_join(rt_thread, NULL);
	check_no_error("pthread_join", err);

	err = pthread_mutex_destroy(&prio_invert);
	check_no_error("pthread_mutex_destroy", err);

	err = sem_destroy(&send_signal);
	check_no_error("sem_destroy", err);

	if (wd) {
		fprintf(wd, "%d", old_wd_value);
		fclose(wd);
	}

	return 0;
}

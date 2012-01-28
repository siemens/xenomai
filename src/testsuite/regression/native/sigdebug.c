/*
 * Functional testing of unwanted domain switch debugging mechanism.
 *
 * Copyright (C) Jan Kiszka  <jan.kiszka@siemens.com>
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
#include <rtdk.h>
#include <native/task.h>
#include <native/mutex.h>
#include <native/sem.h>
#include <native/timer.h>

unsigned int expected_reason;
bool sigdebug_received;
pthread_t rt_task_thread;
RT_MUTEX prio_invert;
RT_SEM send_signal;
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

	rt_task_set_mode(T_WARNSW, 0, NULL);
	rt_print_flush_buffers();
	fprintf(stderr, "FAILURE %s:%d: %s returned %d instead of %d - %s\n",
		fn, line, msg, status, expected, strerror(-status));
	exit(EXIT_FAILURE);
}

static void check_sigdebug_inner(const char *fn, int line, const char *reason)
{
	if (sigdebug_received)
		return;

	rt_task_set_mode(T_WARNSW, 0, NULL);
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

void rt_task_body(void *cookie)
{
	RTIME end;
	int err;

	rt_task_thread = pthread_self();

	rt_printf("syscall\n");
	setup_checkdebug(SIGDEBUG_MIGRATE_SYSCALL);
	sched_yield();
	check_sigdebug_received("SIGDEBUG_MIGRATE_SYSCALL");

	rt_printf("signal\n");
	setup_checkdebug(SIGDEBUG_MIGRATE_SIGNAL);
	err = rt_sem_v(&send_signal);
	check_no_error("rt_sem_v", err);
	rt_task_sleep(10000000LL);
	check_sigdebug_received("SIGDEBUG_MIGRATE_SIGNAL");

	rt_printf("relaxed mutex owner\n");
	setup_checkdebug(SIGDEBUG_MIGRATE_PRIOINV);
	err = rt_mutex_acquire(&prio_invert, TM_INFINITE);
	check("rt_mutex_acquire", err, -EINTR);
	check_sigdebug_received("SIGDEBUG_MIGRATE_PRIOINV");

	rt_printf("page fault\n");
	setup_checkdebug(SIGDEBUG_MIGRATE_FAULT);
	rt_task_sleep(0);
	*mem ^= 0xFF;
	check_sigdebug_received("SIGDEBUG_MIGRATE_FAULT");

	if (wd) {
		rt_printf("watchdog\n");
		rt_print_flush_buffers();
		setup_checkdebug(SIGDEBUG_WATCHDOG);
		end = rt_timer_tsc() + rt_timer_ns2tsc(2100000000ULL);
		rt_task_sleep(0);
		while (rt_timer_tsc() < end && !sigdebug_received)
			/* busy loop */;
		check_sigdebug_received("SIGDEBUG_WATCHDOG");
	}
}

void sigdebug_handler(int sig, siginfo_t *si, void *context)
{
	unsigned int reason = si->si_value.sival_int;

	if (reason != expected_reason) {
		rt_print_flush_buffers();
		fprintf(stderr, "FAILURE: sigdebug_handler expected reason %d,"
			" received %d\n", expected_reason, reason);
		exit(EXIT_FAILURE);
	}
	sigdebug_received = true;
}

void dummy_handler(int sig, siginfo_t *si, void *context)
{
}

int main(int argc, char **argv)
{
	char tempname[] = "/tmp/sigdebug-XXXXXX";
	char buf[BUFSIZ], dev[BUFSIZ];
	RT_TASK main_task, rt_task;
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
		wd = fopen("/sys/module/xeno_nucleus/parameters/"
			   "watchdog_timeout", "w+");
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

	printf("mlockall\n");
	setup_checkdebug(SIGDEBUG_NOMLOCK);
	err = rt_task_shadow(&main_task, "main_task", 0, 0);
	check("rt_task_shadow", err, -EINTR);
	check_sigdebug_received("SIGDEBUG_NOMLOCK");

	mlockall(MCL_CURRENT | MCL_FUTURE);

	errno = 0;
	tmp_fd = mkstemp(tempname);
	check_no_error("mkstemp", -errno);
	unlink(tempname);
	check_no_error("unlink", -errno);
	mem = mmap(NULL, 1, PROT_READ | PROT_WRITE, MAP_SHARED, tmp_fd, 0);
	check_no_error("mmap", -errno);
	err = write(tmp_fd, "X", 1);
	check("write", err, 1);

	err = rt_task_shadow(&main_task, "main_task", 0, 0);
	check_no_error("rt_task_shadow", err);

	err = rt_mutex_create(&prio_invert, "prio_invert");
	check_no_error("rt_mutex_create", err);

	err = rt_mutex_acquire(&prio_invert, TM_INFINITE);
	check_no_error("rt_mutex_acquire", err);

	err = rt_sem_create(&send_signal, "send_signal", 0, S_PRIO);
	check_no_error("rt_sem_create", err);

	err = rt_task_spawn(&rt_task, "rt_task", 0, 1, T_WARNSW | T_JOINABLE,
			    rt_task_body, NULL);
	check_no_error("rt_task_spawn", err);

	err = rt_sem_p(&send_signal, TM_INFINITE);
	check_no_error("rt_sem_signal", err);
	pthread_kill(rt_task_thread, SIGUSR1);

	rt_task_sleep(20000000LL);

	err = rt_mutex_release(&prio_invert);
	check_no_error("rt_mutex_release", err);

	err = rt_task_join(&rt_task);
	check_no_error("rt_task_join", err);

	err = rt_mutex_delete(&prio_invert);
	check_no_error("rt_mutex_delete", err);

	err = rt_sem_delete(&send_signal);
	check_no_error("rt_sem_delete", err);

	if (wd) {
		fprintf(wd, "%d", old_wd_value);
		fclose(wd);
	}

	fprintf(stderr, "Test OK\n");

	return 0;
}

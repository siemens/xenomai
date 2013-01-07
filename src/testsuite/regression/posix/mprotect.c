/*
 * Test if implicit pinning of memory via mprotect works.
 *
 * Copyright (C) Jan Kiszka  <jan.kiszka@siemens.com>
 *
 * Released under the terms of GPLv2.
 */

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sys/mman.h>
#include <pthread.h>

#include "check.h"

#define MEMSIZE 0x10000

static void check_value_inner(const char *fn, int line, const char *msg,
			      int value, int expected)
{
	if (value == expected)
		return;

	pthread_set_mode_np(PTHREAD_WARNSW, 0);
	fprintf(stderr,
		"FAILURE %s:%d: %s returned %u instead of %u\n",
		fn, line, msg, value, expected);
	exit(EXIT_FAILURE);
}

#define check_value(msg, value, expected) do {				\
	int __value = value;						\
	check_value_inner(__FUNCTION__, __LINE__, msg, __value, 	\
			  expected);					\
} while (0)

void sigdebug_handler(int sig, siginfo_t *si, void *context)
{
	unsigned int reason = si->si_value.sival_int;

	fprintf(stderr, "FAILURE: sigdebug_handler triggered, reason %d\n",
		reason);
	exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
	unsigned char *zero_mem, *test1_mem, *test2_mem;
	struct sched_param param = { .sched_priority = 1 };
	struct timespec zero = { .tv_sec = 0, .tv_nsec = 0 };
	struct sigaction sa;

	zero_mem = check_mmap(mmap(0, MEMSIZE, PROT_READ,
			      MAP_PRIVATE | MAP_ANONYMOUS, 0, 0));
	test1_mem = check_mmap(mmap(0, MEMSIZE, PROT_READ,
				    MAP_PRIVATE | MAP_ANONYMOUS, 0, 0));

	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = sigdebug_handler;
	sa.sa_flags = SA_SIGINFO;
	check_unix(sigaction(SIGDEBUG, &sa, NULL));

	check_unix(mlockall(MCL_CURRENT | MCL_FUTURE));

	check_pthread(pthread_setschedparam(pthread_self(), SCHED_FIFO, &param));

	printf("memory read\n");
	check_value("read mem", test1_mem[0], 0);

	pthread_set_mode_np(PTHREAD_WARNSW, 0);
	test2_mem = check_mmap(mmap(0, MEMSIZE, PROT_READ | PROT_WRITE,
				    MAP_PRIVATE | MAP_ANONYMOUS, 0, 0));
	check_unix(mprotect(test2_mem, MEMSIZE,
			    PROT_READ | PROT_WRITE | PROT_EXEC));

	nanosleep(&zero, NULL);
	pthread_set_mode_np(0, PTHREAD_WARNSW);

	printf("memory write after exec enable\n");
	test2_mem[0] = 0xff;

	pthread_set_mode_np(PTHREAD_WARNSW, 0);
	check_unix(mprotect(test1_mem, MEMSIZE, PROT_READ | PROT_WRITE));

	nanosleep(&zero, NULL);
	pthread_set_mode_np(0, PTHREAD_WARNSW);

	printf("memory write after write enable\n");
	test1_mem[0] = 0xff;
	check_value("read zero", zero_mem[0], 0);

	pthread_set_mode_np(PTHREAD_WARNSW, 0);

	test1_mem = check_mmap(mmap(0, MEMSIZE, PROT_NONE,
				    MAP_PRIVATE | MAP_ANONYMOUS, 0, 0));
	check_unix(mprotect(test1_mem, MEMSIZE, PROT_READ | PROT_WRITE));

	printf("memory read/write after access enable\n");
	check_value("read mem", test1_mem[0], 0);
	test1_mem[0] = 0xff;
	check_value("read zero", zero_mem[0], 0);

	fprintf(stderr, "Test OK\n");

	return 0;
}

/*
 * Copyright (C) 2014 Philippe Gerum <rpm@xenomai.org>
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <limits.h>
#include <time.h>
#include <signal.h>
#include <error.h>
#include <sys/cobalt.h>
#include <rtdm/autotune.h>
#include <xenomai/init.h>

static int tune_irqlat, tune_kernlat, tune_userlat;

static int reset, noload, background;

/*
 * --verbosity_level=0 means fully quiet, =1 means almost quiet.
 */
#define verbose (__base_setup_data.verbosity_level)

static const struct option base_options[] = {
	{
#define irq_opt		0
		.name = "irq",
		.has_arg = no_argument,
		.flag = &tune_irqlat,
		.val = 1
	},
	{
#define kernel_opt	1
		.name = "kernel",
		.has_arg = no_argument,
		.flag = &tune_kernlat,
		.val = 1
	},
	{
#define user_opt	2
		.name = "user",
		.has_arg = no_argument,
		.flag = &tune_userlat,
		.val = 1
	},
	{
#define reset_opt	3
		.name = "reset",
		.has_arg = no_argument,
		.flag = &reset,
		.val = 1
	},
	{
#define noload_opt	4
		.name = "noload",
		.has_arg = no_argument,
		.flag = &noload,
		.val = 1
	},
	{
#define period_opt	5
		.name = "period",
		.has_arg = required_argument,
	},
	{
#define background_opt	6
		.name = "background",
		.has_arg = no_argument,
		.flag = &background,
		.val = 1,
	},
	{ /* Sentinel */ }
};

static void *sampler_thread(void *arg)
{
	int fd = (long)arg, ret, n = 0;
	__u64 timestamp = 0;
	struct timespec now;

	for (;;) {
		ret = ioctl(fd, AUTOTUNE_RTIOC_PULSE, &timestamp);
		if (ret) {
			if (errno != EPIPE)
				error(1, errno, "pulse failed");
			timestamp = 0; /* Next tuning period. */
			n = 0;
		} else {
			n++;
			clock_gettime(CLOCK_MONOTONIC, &now);
			timestamp = (__u64)now.tv_sec * 1000000000 + now.tv_nsec;
		}
	}

	return NULL;
}

static void *load_thread(void *arg)
{
	int fdi, fdo, count = 0, wakelim;
	ssize_t nbytes, ret;
	struct timespec rqt;
	char buf[512];

	fdi = open("/dev/zero", O_RDONLY);
	if (fdi < 0)
		error(1, errno, "/dev/zero");

	fdo = open("/dev/null", O_WRONLY);
	if (fdi < 0)
		error(1, errno, "/dev/null");

	rqt.tv_sec = 0;
	rqt.tv_nsec = CONFIG_XENO_DEFAULT_PERIOD * 2;
	wakelim = 20000000 / rqt.tv_nsec;

	for (;;) {
		clock_nanosleep(CLOCK_MONOTONIC, 0, &rqt, NULL);

		if ((++count % wakelim) == 0) {
			cobalt_thread_relax();
			continue;
		}

		nbytes = read(fdi, buf, sizeof(buf));
		if (nbytes <= 0)
			error(1, EIO, "load streaming");
		if (nbytes > 0) {
			ret = write(fdo, buf, nbytes);
			(void)ret;
		}
	}

	return NULL;
}

static void create_sampler(pthread_t *tid, int fd)
{
	struct sched_param param;
	pthread_attr_t attr;
	int ret;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
	param.sched_priority = 99;
	pthread_attr_setschedparam(&attr, &param);
	ret = pthread_create(tid, &attr, sampler_thread, (void *)(long)fd);
	if (ret)
		error(1, ret, "sampling thread");

	pthread_attr_destroy(&attr);
	pthread_setname_np(*tid, "sampler");
}

static void create_load(pthread_t *tid)
{
	struct sched_param param;
	pthread_attr_t attr;
	int ret;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
	param.sched_priority = 1;
	pthread_attr_setschedparam(&attr, &param);
	ret = pthread_create(tid, &attr, load_thread, NULL);
	if (ret)
		error(1, ret, "load thread");

	pthread_attr_destroy(&attr);
	pthread_setname_np(*tid, "loadgen");
}

void application_usage(void)
{
        fprintf(stderr, "usage: %s [options]:\n", get_program_name());
	fprintf(stderr, "--irq				tune for interrupt latency\n");
	fprintf(stderr, "--kernel			tune for kernel scheduling latency\n");
	fprintf(stderr, "--user				tune for user scheduling latency\n");
	fprintf(stderr, "    [ if none of --irq, --kernel and --user is given,\n"
  		        "      tune for all contexts ]\n");
	fprintf(stderr, "--period			set the sampling period\n");
	fprintf(stderr, "--reset 			reset core timer gravity to factory defaults\n");
	fprintf(stderr, "--noload			disable load generation\n");
	fprintf(stderr, "--background 			run in the background\n");
}

static void run_tuner(int fd, unsigned int op, int period, const char *type)
{
	struct autotune_setup setup;
	pthread_t sampler;
	__u32 gravity;
	int ret;

	setup.period = period;
	setup.quiet = verbose > 2 ? 0 : 2 - verbose;
	ret = ioctl(fd, op, &setup);
	if (ret)
		error(1, errno, "setup failed (%s)", type);

	if (verbose) {
		printf("%s gravity... ", type);
		fflush(stdout);
	}

	if (op == AUTOTUNE_RTIOC_USER)
		create_sampler(&sampler, fd);

	ret = ioctl(fd, AUTOTUNE_RTIOC_RUN, &gravity);
	if (ret)
		error(1, errno, "tuning failed (%s)", type);

	if (op == AUTOTUNE_RTIOC_USER)
		pthread_cancel(sampler);

	if (verbose)
		printf("%u ns\n", gravity);
}

int main(int argc, char *const argv[])
{
	int fd, period, ret, c, lindex, tuned = 0;
	pthread_t load_pth;
	cpu_set_t cpu_set;
	time_t start;

	period = CONFIG_XENO_DEFAULT_PERIOD;

	for (;;) {
		c = getopt_long_only(argc, argv, "", base_options, &lindex);
		if (c == EOF)
			break;
		if (c == '?') {
			xenomai_usage();
			return EINVAL;
		}
		if (c > 0)
			continue;

		switch (lindex) {
		case period_opt:
			period = atoi(optarg);
			if (period <= 0)
				error(1, EINVAL, "invalid sampling period (default %d)",
				      CONFIG_XENO_DEFAULT_PERIOD);
			break;
		case noload_opt:
		case background_opt:
			break;
		case irq_opt:
		case kernel_opt:
		case user_opt:
		case reset_opt:
			tuned = 1;
			break;
		default:
			return EINVAL;
		}
	}

	CPU_ZERO(&cpu_set);
	CPU_SET(0, &cpu_set);
	ret = sched_setaffinity(0, sizeof(cpu_set), &cpu_set);
	if (ret)
		error(1, errno, "cannot set CPU affinity");

	if (background) {
		signal(SIGHUP, SIG_IGN);
		ret = daemon(0, 0);
		if (ret)
			error(1, errno, "cannot daemonize");
	}

	fd = open("/dev/rtdm/autotune", O_RDONLY);
	if (fd < 0)
		error(1, errno, "cannot open autotune device");

	if (!tuned)
		tune_irqlat = tune_kernlat = tune_userlat = 1;

	if (reset) {
		ret = ioctl(fd, AUTOTUNE_RTIOC_RESET);
		if (ret)
			error(1, errno, "reset failed");
	}

	if (tune_irqlat || tune_kernlat || tune_userlat) {
		if (!noload)
			create_load(&load_pth);
		if (verbose)
			printf("== auto-tuning started, period=%d ns (may take a while)\n",
			       period);
	} else
		noload = 1;

	time(&start);

	if (tune_irqlat)
		run_tuner(fd, AUTOTUNE_RTIOC_IRQ, period, "irq");

	if (tune_kernlat)
		run_tuner(fd, AUTOTUNE_RTIOC_KERN, period, "kernel");

	if (tune_userlat)
		run_tuner(fd, AUTOTUNE_RTIOC_USER, period, "user");

	if (verbose && (tune_userlat || tune_kernlat || tune_userlat))
		printf("== auto-tuning completed after %ds\n",
		       (int)(time(NULL) - start));

	if (!noload)
		pthread_cancel(load_pth);

	close(fd);

	return 0;
}

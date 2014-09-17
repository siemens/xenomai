/*
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
#include <xeno_config.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <limits.h>
#include <time.h>
#include <error.h>
#include <rtdm/autotune.h>

static int tune_irqlat, tune_kernlat, tune_userlat;

static int reset, nohog, quiet;

static const struct option base_options[] = {
	{
#define help_opt	0
		.name = "help",
		.has_arg = 0,
		.flag = NULL,
		.val = 0
	},
	{
#define irq_opt		1
		.name = "irq",
		.flag = &tune_irqlat,
		.val = 1
	},
	{
#define kernel_opt	2
		.name = "kernel",
		.flag = &tune_kernlat,
		.val = 1
	},
	{
#define user_opt	3
		.name = "user",
		.flag = &tune_userlat,
		.val = 1
	},
	{
#define reset_opt	4
		.name = "reset",
		.flag = &reset,
		.val = 1
	},
	{
#define nohog_opt	5
		.name = "nohog",
		.flag = &nohog,
		.val = 1
	},
	{
#define quiet_opt	6
		.name = "quiet",
		.flag = &quiet,
		.val = 1
	},
	{
#define period_opt	7
		.name = "period",
		.has_arg = 1,
	},
	{
		.name = NULL,
	}
};

static void *sampler_thread(void *arg)
{
	nanosecs_abs_t timestamp = 0;
	int fd = (long)arg, ret, n = 0;
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
			timestamp = (nanosecs_abs_t)now.tv_sec * 1000000000 + now.tv_nsec;
		}
	}

	return NULL;
}

static void *hog_thread(void *arg)
{
	int fdi, fdo, count = 0;
	ssize_t nbytes, ret;
	char buf[512];

	fdi = open("/dev/zero", O_RDONLY);
	if (fdi < 0)
		error(1, errno, "/dev/zero");

	fdo = open("/dev/null", O_WRONLY);
	if (fdi < 0)
		error(1, errno, "/dev/null");

	for (;;) {
		nbytes = read(fdi, buf, sizeof(buf));
		if (nbytes <= 0)
			error(1, EIO, "hog streaming");
		if (nbytes > 0) {
			ret = write(fdo, buf, nbytes);
			(void)ret;
		}
		if ((++count % 1024) == 0)
			usleep(10000);
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
	pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN * 4);
	ret = pthread_create(tid, &attr, sampler_thread, (void *)(long)fd);
	if (ret)
		error(1, ret, "sampling thread");

	pthread_attr_destroy(&attr);
	pthread_setname_np(*tid, "sampler");
}

static void create_hog(pthread_t *tid)
{
	struct sched_param param;
	pthread_attr_t attr;
	int ret;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
	param.sched_priority = 0;
	pthread_attr_setschedparam(&attr, &param);
	pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN * 8);
	ret = pthread_create(tid, &attr, hog_thread, NULL);
	if (ret)
		error(1, ret, "hog thread");

	pthread_attr_destroy(&attr);
	pthread_setname_np(*tid, "hog");
}

static void usage(void)
{
	fprintf(stderr, "usage: autotune [options], tuning core timer for:\n");
	fprintf(stderr, "   --irq		interrupt latency\n");
	fprintf(stderr, "   --kernel		kernel scheduling latency\n");
	fprintf(stderr, "   --user		user scheduling latency\n");
	fprintf(stderr, "   --period		set the sampling period\n");
	fprintf(stderr, "   --reset		reset core timer gravity to factory defaults\n");
	fprintf(stderr, "   --nohog		disable load generation\n");
	fprintf(stderr, "   --quiet		tame down verbosity\n");
	fprintf(stderr, "   --help		print this help\n\n");
	fprintf(stderr, "if no option is given, tune for all contexts using the default period.\n");
}

static void run_tuner(int fd, int op, int period, const char *type)
{
	struct autotune_setup setup;
	unsigned long gravity;
	pthread_t sampler;
	int ret;

	setup.period = period;
	setup.quiet = quiet;
	ret = ioctl(fd, op, &setup);
	if (ret)
		error(1, errno, "setup failed (%s)", type);

	if (!quiet) {
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

	if (!quiet)
		printf("%lu ns\n", gravity);
}

int main(int argc, char *const argv[])
{
	int fd, period, ret, c, lindex, tuned = 0;
	pthread_t hog;

	period = CONFIG_XENO_DEFAULT_PERIOD;

	for (;;) {
		c = getopt_long_only(argc, argv, "", base_options, &lindex);
		if (c == EOF)
			break;
		if (c == '?') {
			usage();
			return EINVAL;
		}
		if (c > 0)
			continue;

		switch (lindex) {
		case help_opt:
			usage();
			exit(0);
		case period_opt:
			period = atoi(optarg);
			if (period <= 0)
				error(1, EINVAL, "invalid sampling period (default %d)",
				      CONFIG_XENO_DEFAULT_PERIOD);
			break;
		case nohog_opt:
		case quiet_opt:
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
		if (!nohog)
			create_hog(&hog);
		if (!quiet)
			printf("Auto-tuning started, period=%dns (may take a while)\n",
				period);
	} else
		nohog = 1;

	if (tune_irqlat)
		run_tuner(fd, AUTOTUNE_RTIOC_IRQ, period, "irq");

	if (tune_kernlat)
		run_tuner(fd, AUTOTUNE_RTIOC_KERN, period, "kernel");

	if (tune_userlat)
		run_tuner(fd, AUTOTUNE_RTIOC_USER, period, "user");

	if (!nohog)
		pthread_cancel(hog);

	close(fd);

	return 0;
}

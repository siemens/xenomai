/*
  Task switch latency test.
  Max Krasnyansky <maxk@qualcomm.com

  Based on latency.c by Philippe Gerum <rpm@xenomai.org>
*/

#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <copperplate/init.h>
#include <alchemy/task.h>
#include <alchemy/timer.h>
#include <alchemy/sem.h>

static RT_TASK event_task, worker_task;

static RT_SEM switch_sem;
static RTIME  switch_tsc;
static unsigned long long switch_count;

static long long minjitter = 10000000;
static long long maxjitter = -10000000;
static long long avgjitter;
static long long lost;
static long long nsamples = 50000;
static long long sampling_period = CONFIG_XENO_DEFAULT_PERIOD;

#define HISTOGRAM_CELLS 100

static unsigned long histogram[HISTOGRAM_CELLS];

static int do_histogram;
static int warmup = 5;
static int late;

static inline void add_histogram(long addval)
{
	/* usec steps */
	long inabs = rt_timer_tsc2ns(addval >= 0 ? addval : -addval) / 1000;
	histogram[inabs < HISTOGRAM_CELLS ? inabs : HISTOGRAM_CELLS - 1]++;
}

void dump_stats(double sum, int total_hits)
{
	int n;
	double avg, variance = 0;

	avg = sum / total_hits;
	for (n = 0; n < HISTOGRAM_CELLS; n++) {
		long hits = histogram[n];
		if (hits)
			variance += hits * (n-avg) * (n-avg);
	}

	/* compute std-deviation (unbiased form) */
	variance /= total_hits - 1;
	variance = sqrt(variance);

	printf("HSS| %9d| %10.3f| %10.3f\n", total_hits, avg, variance);
}

void dump_histogram(void)
{
	int n, total_hits = 0;
	double sum = 0;
	fprintf(stderr, "---|---range-|---samples\n");
	for (n = 0; n < HISTOGRAM_CELLS; n++) {
		long hits = histogram[n];
		if (hits) {
			total_hits += hits;
			sum += n * hits;
			fprintf(stderr, "HSD| %d - %d | %10ld\n",
				n, n + 1, hits);
		}
	}
	dump_stats(sum, total_hits);
}

void event(void *cookie)
{
	int err;

	err = rt_task_set_periodic(NULL,
				   TM_NOW,
				   rt_timer_ns2ticks(sampling_period));
	if (err) {
		warning("failed to enter periodic timing (%s)\n",
			symerror(err));
		return;
	}

	for (;;) {
		err = rt_task_wait_period(NULL);
		if (err) {
			if (err != -ETIMEDOUT)
			       exit(EXIT_FAILURE);
			late++;
		}

		switch_count++;
		err = rt_sem_broadcast(&switch_sem);
		switch_tsc = rt_timer_tsc();
		if (err) {
			if (err != -EIDRM && err != -EINVAL)
				warning("failed to broadcast semaphore (%s)\n",
					symerror(err));
			break;
		}
	}
}

void worker(void *cookie)
{
	long long minj = 10000000, maxj = -10000000, dt, sumj = 0;
	unsigned long long count = 0;
	int err, n;

	err = rt_sem_create(&switch_sem, "dispsem", 0, S_FIFO);
	if (err) {
		warning("failed to create semaphore (%s)\n",
			symerror(err));
		return;
	}

	for (n = 0; n < nsamples; n++) {
		err = rt_sem_p(&switch_sem, TM_INFINITE);
		if (err) {
			if (err != -EIDRM && err != -EINVAL)
				warning("failed to pend on semaphore (%s)\n",
					symerror(err));
			exit(EXIT_FAILURE);
		}

		dt = (long) (rt_timer_tsc() - switch_tsc);

		if (switch_count - count > 1) {
			lost += switch_count - count;
			count = switch_count;
			continue;
		}

		if (++count < warmup)
			continue;

		if (dt > maxj)
			maxj = dt;
		if (dt < minj)
			minj = dt;
		sumj += dt;

		if (do_histogram)
			add_histogram(dt);
	}

	rt_sem_delete(&switch_sem);

	minjitter = minj;
	maxjitter = maxj;
	avgjitter = sumj / n;

	printf("RTH|%12s|%12s|%12s|%12s\n",
	       "lat min", "lat avg", "lat max", "lost");

	printf("RTD|%12.3f|%12.3f|%12.3f|%12lld\n",
	       rt_timer_tsc2ns(minjitter) / 1000.0,
	       rt_timer_tsc2ns(avgjitter) / 1000.0,
	       rt_timer_tsc2ns(maxjitter) / 1000.0, lost);

	if (late)
		printf("LATE: %d\n", late);

	if (do_histogram)
		dump_histogram();

	exit(0);
>>>>>>> testsuite/unit: sanitize wakeup-time
}

int main(int argc, char **argv)
{
	int err, c;

	copperplate_init(argc, argv);

	while ((c = getopt(argc, argv, "hp:n:i:")) != EOF)
		switch (c) {
		case 'h':
			/* ./switch --h[istogram] */
			do_histogram = 1;
			break;

		case 'p':
			sampling_period = atoi(optarg) * 1000;
			break;

		case 'n':
			nsamples = atoi(optarg);
			break;

		case 'i':
			warmup = atoi(optarg);
			break;

		default:

			fprintf(stderr, "usage: switch [options]\n"
				"\t-h		  - enable histogram\n"
				"\t-p <period_us> - timer period\n"
				"\t-n <samples>	  - number of samples to collect\n"
				"\t-i <samples>	  - number of _first_ samples to ignore\n");
			exit(2);
		}

	if (sampling_period == 0)
		sampling_period = 100000;	/* ns */

	if (nsamples <= 0) {
		warning("disregarding -n <%lld>, using -n <100000> "
			"samples\n", nsamples);
		nsamples = 100000;
	}

	signal(SIGINT, SIG_IGN);
	signal(SIGTERM, SIG_IGN);

	setlinebuf(stdout);

	mlockall(MCL_CURRENT|MCL_FUTURE);

	printf("== Sampling period: %llu us\n", sampling_period / 1000);
	printf("== Do not interrupt this program\n");

	err = rt_task_create(&worker_task, "worker", 0, 98, 0);
	if (err) {
		warning("failed to create WORKER task (%s)\n",
			symerror(err));
		return 1;
	}

	err = rt_task_start(&worker_task, &worker, NULL);
	if (err) {
		warning("failed to start WORKER task (%s)\n",
			symerror(err));
		return 1;
	}

	err = rt_task_create(&event_task, "event", 0, 99, 0);
	if (err) {
		warning("failed to create EVENT task (%s)\n",
			symerror(err));
		return 1;
	}

	err = rt_task_start(&event_task, &event, NULL);
	if (err) {
		warning("failed to start EVENT task (%s)\n",
			symerror(err));
		return 1;
	}

	for (;;)
		pause();

	return 0;
}

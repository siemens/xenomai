#include <stdio.h>
#include <pthread.h>
#include <smokey/smokey.h>
#include <asm/xenomai/features.h>
#include <asm/xenomai/uapi/fptest.h>

smokey_test_plugin(fpu_stress,
		   SMOKEY_ARGLIST(
			   SMOKEY_INT(duration),
		   ),
		   "Check FPU context sanity during real-time stress\n"
		   "\tduration=<seconds>\thow long to run the stress loop (0=indefinitely)"
);

static int fp_features;

static void *stress_loop(void *arg)
{
	struct timespec rqt = {
		.tv_sec = 0,
		.tv_nsec = CONFIG_XENO_DEFAULT_PERIOD
	};
	
	for (;;) {
		fp_regs_set(fp_features, 0xf1f5f1f5);
		clock_nanosleep(CLOCK_MONOTONIC, 0, &rqt, NULL);
	}

	return NULL;
}

static int report_error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	smokey_vatrace(fmt, ap);
	va_end(ap);

	return 0;
}

static int run_fpu_stress(struct smokey_test *t,
			  int argc, char *const argv[])
{
	unsigned sleep_ms, n, rounds, duration = 3;
	struct sched_param param;
	pthread_attr_t attr;
	struct timespec rqt;
	pthread_t tid;
	int ret;

	fp_features = cobalt_fp_detect();
	if (fp_features == 0)
		return -ENOSYS;

	smokey_parse_args(t, argc, argv);
	
	if (SMOKEY_ARG_ISSET(fpu_stress, duration))
		duration = SMOKEY_ARG_INT(fpu_stress, duration);
	
	rqt.tv_sec = 0;
	rqt.tv_nsec = CONFIG_XENO_DEFAULT_PERIOD;
	sleep_ms = 1000000UL / rqt.tv_nsec; /* wake up each ms */
	rounds = duration * 1000UL / sleep_ms;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
	param.sched_priority = 10;
	pthread_attr_setschedparam(&attr, &param);
	ret = pthread_create(&tid, &attr, stress_loop, NULL);
	if (ret)
		return -ret;

	if (rounds)
		smokey_trace("running for %d seconds", duration);
	else
		smokey_trace("running indefinitely...");

	for (n = 0; rounds == 0 || n < rounds; n++) {
		fp_regs_set(fp_features, n);
		__STD(clock_nanosleep(CLOCK_MONOTONIC, 0, &rqt, NULL));
		if (fp_regs_check(fp_features, n, report_error) != n) {
			ret = -EINVAL;
			break;
		}
	}

	pthread_cancel(tid);
	pthread_join(tid, NULL);

	return ret;
}

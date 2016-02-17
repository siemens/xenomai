/*
 * Functional testing of the mutex implementation for Cobalt.
 *
 * Copyright (C) Gilles Chanteperdrix  <gilles.chanteperdrix@xenomai.org>,
 *               Marion Deveaud <marion.deveaud@siemens.com>,
 *               Jan Kiszka <jan.kiszka@siemens.com>
 *
 * Released under the terms of GPLv2.
 */
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <cobalt/sys/cobalt.h>
#include <cobalt/uapi/syscall.h>
#include "lib/cobalt/current.h"
#include <smokey/smokey.h>

smokey_test_plugin(posix_mutex,
		   SMOKEY_NOARGS,
		   "Check POSIX mutex services"
);

#define MUTEX_CREATE		1
#define MUTEX_LOCK		2
#define MUTEX_TRYLOCK		3
#define MUTEX_TIMED_LOCK	4
#define MUTEX_UNLOCK		5
#define MUTEX_DESTROY		6
#define COND_CREATE		7
#define COND_SIGNAL		8
#define COND_WAIT		9
#define COND_DESTROY		10
#define THREAD_DETACH		11
#define THREAD_CREATE		12
#define THREAD_JOIN		13
#define THREAD_RENICE           14

#define NS_PER_MS	1000000

static const char *reason_str[] = {
	[SIGDEBUG_UNDEFINED] = "undefined",
	[SIGDEBUG_MIGRATE_SIGNAL] = "received signal",
	[SIGDEBUG_MIGRATE_SYSCALL] = "invoked syscall",
	[SIGDEBUG_MIGRATE_FAULT] = "triggered fault",
	[SIGDEBUG_MIGRATE_PRIOINV] = "affected by priority inversion",
	[SIGDEBUG_NOMLOCK] = "missing mlockall",
	[SIGDEBUG_WATCHDOG] = "runaway thread",
};

static void sigdebug(int sig, siginfo_t *si, void *context)
{
	unsigned int reason = sigdebug_reason(si);

	smokey_trace("\nSIGDEBUG received, reason %d: %s\n", reason,
		     reason <= SIGDEBUG_WATCHDOG ? reason_str[reason] : "<unknown>");
}

static inline unsigned long long timer_get_tsc(void)
{
	return clockobj_get_tsc();
}

static inline unsigned long long timer_tsc2ns(unsigned long long tsc)
{
	return clockobj_tsc_to_ns(tsc);
}

static void add_timespec(struct timespec *ts, unsigned long long value)
{
	ts->tv_sec += value / 1000000000;
	ts->tv_nsec += value % 1000000000;
	if (ts->tv_nsec > 1000000000) {
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000;
	}
}

static void ms_sleep(int time)
{
	struct timespec ts;

	ts.tv_sec = 0;
	ts.tv_nsec = time*NS_PER_MS;

	nanosleep(&ts, NULL);
}

static void check_current_prio(int expected_prio)
{
	struct cobalt_threadstat stat;
	int ret;

	ret = cobalt_thread_stat(0, &stat);
	if (ret) {
		fprintf(stderr,
			"FAILURE: cobalt_threadstat (%s)\n", strerror(-ret));
		exit(EXIT_FAILURE);
	}

	if (stat.cprio != expected_prio) {
		fprintf(stderr,
			"FAILURE: current prio (%d) != expected prio (%d)\n",
			stat.cprio, expected_prio);
		exit(EXIT_FAILURE);
	}
}

static void __check_current_mode(const char *file, int line,
				 int mask, int expected_value)
{
	int current_mode;

	/* This is a unit test, and in this circonstance, we are allowed to
	   call cobalt_get_current_mode. But please do not do that in your
	   own code. */
	current_mode = cobalt_get_current_mode() & mask;

	if (current_mode != expected_value) {
		fprintf(stderr,
			"FAILURE at %s:%d: current mode (%x) != expected mode (%x)\n",
			file, line, current_mode, expected_value);
		exit(EXIT_FAILURE);
	}
}

#define check_current_mode(mask, expected_value)	\
	__check_current_mode(__FILE__, __LINE__, (mask), (expected_value))

static int dispatch(const char *service_name,
		    int service_type, int check, int expected, ...)
{
	unsigned long long timeout;
	pthread_t *thread;
	pthread_cond_t *cond;
	void *handler;
	va_list ap;
	int status, protocol, type;
	pthread_mutex_t *mutex;
	struct sched_param param;
	pthread_attr_t threadattr;
	pthread_mutexattr_t mutexattr;
	struct timespec ts;

	va_start(ap, expected);
	switch (service_type) {
	case MUTEX_CREATE:
		mutex = va_arg(ap, pthread_mutex_t *);
		pthread_mutexattr_init(&mutexattr);
		protocol = va_arg(ap, int);
		/* May fail if unsupported, that's ok. */
		pthread_mutexattr_setprotocol(&mutexattr, protocol);
		type = va_arg(ap, int);
		pthread_mutexattr_settype(&mutexattr, type);
		status = pthread_mutex_init(mutex, &mutexattr);
		break;

	case MUTEX_LOCK:
		status = pthread_mutex_lock(va_arg(ap, pthread_mutex_t *));
		break;

	case MUTEX_TRYLOCK:
		status = pthread_mutex_trylock(va_arg(ap, pthread_mutex_t *));
		break;

	case MUTEX_TIMED_LOCK:
		mutex = va_arg(ap, pthread_mutex_t *);
		timeout = va_arg(ap, unsigned long long);
		clock_gettime(CLOCK_REALTIME, &ts);
		add_timespec(&ts, timeout);
		status = pthread_mutex_timedlock(mutex, &ts);
		break;

	case MUTEX_UNLOCK:
		status = pthread_mutex_unlock(va_arg(ap, pthread_mutex_t *));
		break;

	case MUTEX_DESTROY:
		status = pthread_mutex_destroy(va_arg(ap, pthread_mutex_t *));
		break;

	case COND_CREATE:
		status = pthread_cond_init(va_arg(ap, pthread_cond_t *), NULL);
		break;

	case COND_SIGNAL:
		status = pthread_cond_signal(va_arg(ap, pthread_cond_t *));
		break;

	case COND_WAIT:
		cond = va_arg(ap, pthread_cond_t *);
		status =
		    pthread_cond_wait(cond, va_arg(ap, pthread_mutex_t *));
		break;

	case COND_DESTROY:
		status = pthread_cond_destroy(va_arg(ap, pthread_cond_t *));
		break;

	case THREAD_DETACH:
		status = pthread_detach(pthread_self());
		break;

	case THREAD_CREATE:
		thread = va_arg(ap, pthread_t *);
		pthread_attr_init(&threadattr);
		param.sched_priority = va_arg(ap, int);
		if (param.sched_priority)
			pthread_attr_setschedpolicy(&threadattr, SCHED_FIFO);
		else
			pthread_attr_setschedpolicy(&threadattr, SCHED_OTHER);
		pthread_attr_setschedparam(&threadattr, &param);
		pthread_attr_setinheritsched(&threadattr,
					     PTHREAD_EXPLICIT_SCHED);
		handler = va_arg(ap, void *);
		status = pthread_create(thread, &threadattr, handler,
					va_arg(ap, void *));
		break;

	case THREAD_JOIN:
		thread = va_arg(ap, pthread_t *);
		status = pthread_join(*thread, NULL);
		break;

	case THREAD_RENICE:
		param.sched_priority = va_arg(ap, int);
		if (param.sched_priority)
			status = pthread_setschedparam(pthread_self(),
						       SCHED_FIFO, &param);
		else
			status = pthread_setschedparam(pthread_self(),
						       SCHED_OTHER, &param);
		break;

	default:
		fprintf(stderr, "Unknown service %i.\n", service_type);
		exit(EXIT_FAILURE);
	}
	va_end(ap);

	if (check && status != expected) {
		fprintf(stderr, "FAILURE: %s: %i (%s) instead of %i\n",
			service_name, status, strerror(status), expected);
		exit(EXIT_FAILURE);
	}
	return status;
}

static void *waiter(void *cookie)
{
	pthread_mutex_t *mutex = (pthread_mutex_t *) cookie;
	unsigned long long start, diff;

	dispatch("waiter pthread_detach", THREAD_DETACH, 1, 0);
	start = timer_get_tsc();
	dispatch("waiter mutex_lock", MUTEX_LOCK, 1, 0, mutex);
	diff = timer_tsc2ns(timer_get_tsc() - start);
	if (diff < 10000000) {
		fprintf(stderr, "FAILURE: waiter, waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}
	ms_sleep(11);
	dispatch("waiter mutex_unlock", MUTEX_UNLOCK, 1, 0, mutex);

	return cookie;
}

static void autoinit_simple_wait(void)
{
	unsigned long long start, diff;
	pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	pthread_t waiter_tid;

	smokey_trace("%s", __func__);

	dispatch("simple mutex_lock 1", MUTEX_LOCK, 1, 0, &mutex);
	dispatch("simple thread_create", THREAD_CREATE, 1, 0, &waiter_tid, 2,
		 waiter, &mutex);
	ms_sleep(11);
	dispatch("simple mutex_unlock 1", MUTEX_UNLOCK, 1, 0, &mutex);
	sched_yield();

	start = timer_get_tsc();
	dispatch("simple mutex_lock 2", MUTEX_LOCK, 1, 0, &mutex);
	diff = timer_tsc2ns(timer_get_tsc() - start);
	if (diff < 10000000) {
		fprintf(stderr, "FAILURE: main, waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}

	dispatch("simple mutex_unlock 2", MUTEX_UNLOCK, 1, 0, &mutex);
	dispatch("simple mutex_destroy", MUTEX_DESTROY, 1, 0, &mutex);
}

static void simple_wait(void)
{
	unsigned long long start, diff;
	pthread_mutex_t mutex;
	pthread_t waiter_tid;

	smokey_trace("%s", __func__);

	dispatch("simple mutex_init", MUTEX_CREATE, 1, 0, &mutex,
		 PTHREAD_PRIO_NONE, PTHREAD_MUTEX_NORMAL);
	dispatch("simple mutex_lock 1", MUTEX_LOCK, 1, 0, &mutex);
	dispatch("simple thread_create", THREAD_CREATE, 1, 0, &waiter_tid, 2,
		 waiter, &mutex);
	ms_sleep(11);
	dispatch("simple mutex_unlock 1", MUTEX_UNLOCK, 1, 0, &mutex);
	sched_yield();

	start = timer_get_tsc();
	dispatch("simple mutex_lock 2", MUTEX_LOCK, 1, 0, &mutex);
	diff = timer_tsc2ns(timer_get_tsc() - start);
	if (diff < 10000000) {
		fprintf(stderr, "FAILURE: main, waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}

	dispatch("simple mutex_unlock 2", MUTEX_UNLOCK, 1, 0, &mutex);
	dispatch("simple mutex_destroy", MUTEX_DESTROY, 1, 0, &mutex);
}

static void autoinit_recursive_wait(void)
{
	unsigned long long start, diff;
	pthread_mutex_t mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
	pthread_t waiter_tid;

	smokey_trace("%s", __func__);

	dispatch("rec mutex_lock 1", MUTEX_LOCK, 1, 0, &mutex);
	dispatch("rec mutex_lock 2", MUTEX_LOCK, 1, 0, &mutex);

	dispatch("rec thread_create", THREAD_CREATE, 1, 0, &waiter_tid, 2,
		 waiter, &mutex);

	dispatch("rec mutex_unlock 2", MUTEX_UNLOCK, 1, 0, &mutex);
	ms_sleep(11);
	dispatch("rec mutex_unlock 1", MUTEX_UNLOCK, 1, 0, &mutex);
	sched_yield();

	start = timer_get_tsc();
	dispatch("rec mutex_lock 3", MUTEX_LOCK, 1, 0, &mutex);
	diff = timer_tsc2ns(timer_get_tsc() - start);

	if (diff < 10000000) {
		fprintf(stderr, "FAILURE: main, waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}
	dispatch("rec mutex_unlock 3", MUTEX_UNLOCK, 1, 0, &mutex);
	dispatch("rec mutex_destroy", MUTEX_DESTROY, 1, 0, &mutex);
}

static void recursive_wait(void)
{
	unsigned long long start, diff;
	pthread_mutex_t mutex;
	pthread_t waiter_tid;

	smokey_trace("%s", __func__);

	dispatch("rec mutex_init", MUTEX_CREATE, 1, 0, &mutex,
		 PTHREAD_PRIO_NONE, PTHREAD_MUTEX_RECURSIVE);
	dispatch("rec mutex_lock 1", MUTEX_LOCK, 1, 0, &mutex);
	dispatch("rec mutex_lock 2", MUTEX_LOCK, 1, 0, &mutex);

	dispatch("rec thread_create", THREAD_CREATE, 1, 0, &waiter_tid, 2,
		 waiter, &mutex);

	dispatch("rec mutex_unlock 2", MUTEX_UNLOCK, 1, 0, &mutex);
	ms_sleep(11);
	dispatch("rec mutex_unlock 1", MUTEX_UNLOCK, 1, 0, &mutex);
	sched_yield();

	start = timer_get_tsc();
	dispatch("rec mutex_lock 3", MUTEX_LOCK, 1, 0, &mutex);
	diff = timer_tsc2ns(timer_get_tsc() - start);

	if (diff < 10000000) {
		fprintf(stderr, "FAILURE: main, waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}
	dispatch("rec mutex_unlock 3", MUTEX_UNLOCK, 1, 0, &mutex);
	dispatch("rec mutex_destroy", MUTEX_DESTROY, 1, 0, &mutex);
}

static void autoinit_errorcheck_wait(void)
{
	unsigned long long start, diff;
	pthread_mutex_t mutex = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;
	pthread_t waiter_tid;
	int err;

	smokey_trace("%s", __func__);

	dispatch("errorcheck mutex_lock 1", MUTEX_LOCK, 1, 0, &mutex);

	err = pthread_mutex_lock(&mutex);
	if (err != EDEADLK) {
		fprintf(stderr, "FAILURE: errorcheck mutex_lock 2: %s\n",
			strerror(err));
		exit(EXIT_FAILURE);
	}

	dispatch("errorcheck thread_create", THREAD_CREATE, 1, 0, &waiter_tid, 2,
		 waiter, &mutex);
	ms_sleep(11);
	dispatch("errorcheck mutex_unlock 1", MUTEX_UNLOCK, 1, 0, &mutex);
	sched_yield();
	err = pthread_mutex_unlock(&mutex);
	if (err != EPERM) {
		fprintf(stderr, "FAILURE: errorcheck mutex_unlock 2: %s\n",
			strerror(err));
		exit(EXIT_FAILURE);
	}

	start = timer_get_tsc();
	dispatch("errorcheck mutex_lock 3", MUTEX_LOCK, 1, 0, &mutex);
	diff = timer_tsc2ns(timer_get_tsc() - start);
	if (diff < 10000000) {
		fprintf(stderr, "FAILURE: main, waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}
	dispatch("errorcheck mutex_unlock 3", MUTEX_UNLOCK, 1, 0, &mutex);
	dispatch("errorcheck mutex_destroy", MUTEX_DESTROY, 1, 0, &mutex);
}

static void errorcheck_wait(void)
{
	unsigned long long start, diff;
	pthread_mutex_t mutex;
	pthread_t waiter_tid;
	int err;

	smokey_trace("%s", __func__);

	dispatch("errorcheck mutex_init", MUTEX_CREATE, 1, 0, &mutex,
		 PTHREAD_PRIO_NONE, PTHREAD_MUTEX_ERRORCHECK);
	dispatch("errorcheck mutex_lock 1", MUTEX_LOCK, 1, 0, &mutex);

	err = pthread_mutex_lock(&mutex);
	if (err != EDEADLK) {
		fprintf(stderr, "FAILURE: errorcheck mutex_lock 2: %s\n",
			strerror(err));
		exit(EXIT_FAILURE);
	}

	dispatch("errorcheck thread_create", THREAD_CREATE, 1, 0, &waiter_tid, 2,
		 waiter, &mutex);
	ms_sleep(11);
	dispatch("errorcheck mutex_unlock 1", MUTEX_UNLOCK, 1, 0, &mutex);
	sched_yield();
	err = pthread_mutex_unlock(&mutex);
	if (err != EPERM) {
		fprintf(stderr, "FAILURE: errorcheck mutex_unlock 2: %s\n",
			strerror(err));
		exit(EXIT_FAILURE);
	}

	start = timer_get_tsc();
	dispatch("errorcheck mutex_lock 3", MUTEX_LOCK, 1, 0, &mutex);
	diff = timer_tsc2ns(timer_get_tsc() - start);
	if (diff < 10000000) {
		fprintf(stderr, "FAILURE: main, waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}
	dispatch("errorcheck mutex_unlock 3", MUTEX_UNLOCK, 1, 0, &mutex);
	dispatch("errorcheck mutex_destroy", MUTEX_DESTROY, 1, 0, &mutex);
}

static void *timed_waiter(void *cookie)
{
	pthread_mutex_t *mutex = (pthread_mutex_t *) cookie;
	unsigned long long start, diff;

	dispatch("timed_waiter pthread_detach", THREAD_DETACH, 1, 0);

	start = timer_get_tsc();
	dispatch("timed_waiter mutex_timed_lock", MUTEX_TIMED_LOCK, 1,
		 ETIMEDOUT, mutex, 10000000ULL);
	diff = timer_tsc2ns(timer_get_tsc() - start);
	if (diff < 10000000) {
		fprintf(stderr, "FAILURE: timed_waiter, waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}

	return cookie;
}

static void timed_mutex(void)
{
	pthread_mutex_t mutex;
	pthread_t waiter_tid;

	smokey_trace("%s", __func__);

	dispatch("timed_mutex mutex_init", MUTEX_CREATE, 1, 0, &mutex,
		 PTHREAD_PRIO_INHERIT, PTHREAD_MUTEX_NORMAL);
	dispatch("timed_mutex mutex_lock 1", MUTEX_LOCK, 1, 0, &mutex);
	dispatch("timed_mutex thread_create", THREAD_CREATE, 1, 0, &waiter_tid,
		 2, timed_waiter, &mutex);
	ms_sleep(20);
	dispatch("timed_mutex mutex_unlock 1", MUTEX_UNLOCK, 1, 0, &mutex);
	ms_sleep(11);
	dispatch("timed_mutex mutex_destroy", MUTEX_DESTROY, 1, 0, &mutex);

}

static void mode_switch(void)
{
	pthread_mutex_t mutex;

	/* Cause a switch to secondary mode */
	__real_sched_yield();

	smokey_trace("%s", __func__);

	dispatch("switch mutex_init", MUTEX_CREATE, 1, 0, &mutex,
		 PTHREAD_PRIO_INHERIT, PTHREAD_MUTEX_NORMAL);

	check_current_mode(XNRELAX, XNRELAX);

	dispatch("switch mutex_lock", MUTEX_LOCK, 1, 0, &mutex);

	check_current_mode(XNRELAX, 0);

	dispatch("switch mutex_unlock", MUTEX_UNLOCK, 1, 0, &mutex);
	dispatch("switch mutex_destroy", MUTEX_DESTROY, 1, 0, &mutex);
}

static void pi_wait(void)
{
	unsigned long long start, diff;
	pthread_mutex_t mutex;
	pthread_t waiter_tid;

#ifndef HAVE_PTHREAD_MUTEXATTR_SETPROTOCOL
	smokey_note("PTHREAD_PRIO_INHERIT not supported");
	return;
#endif
	smokey_trace("%s", __func__);

	dispatch("pi mutex_init", MUTEX_CREATE, 1, 0, &mutex,
		 PTHREAD_PRIO_INHERIT, PTHREAD_MUTEX_NORMAL);
	dispatch("pi mutex_lock 1", MUTEX_LOCK, 1, 0, &mutex);

	check_current_prio(2);

	/* Give waiter a higher priority than main thread */
	dispatch("pi thread_create", THREAD_CREATE, 1, 0, &waiter_tid, 3, waiter,
		 &mutex);
	ms_sleep(11);

	check_current_prio(3);

	dispatch("pi mutex_unlock 1", MUTEX_UNLOCK, 1, 0, &mutex);

	check_current_prio(2);

	start = timer_get_tsc();
	dispatch("pi mutex_lock 2", MUTEX_LOCK, 1, 0, &mutex);
	diff = timer_tsc2ns(timer_get_tsc() - start);
	if (diff < 10000000) {
		fprintf(stderr, "FAILURE: main, waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}
	dispatch("pi mutex_unlock 2", MUTEX_UNLOCK, 1, 0, &mutex);
	dispatch("pi mutex_destroy", MUTEX_DESTROY, 1, 0, &mutex);
}

static void lock_stealing(void)
{
	pthread_mutex_t mutex;
	pthread_t lowprio_tid;
	int trylock_result;

	/* Main thread acquires the mutex and starts a waiter with lower
	   priority. Then main thread releases the mutex, but locks it again
	   without giving the waiter a chance to get it beforehand. */

	smokey_trace("%s", __func__);

	dispatch("lock_stealing mutex_init", MUTEX_CREATE, 1, 0, &mutex,
		 PTHREAD_PRIO_INHERIT, PTHREAD_MUTEX_NORMAL);
	dispatch("lock_stealing mutex_lock 1", MUTEX_LOCK, 1, 0, &mutex);

	/* Main thread should have higher priority */
	dispatch("lock_stealing thread_create 1", THREAD_CREATE, 1, 0,
		 &lowprio_tid, 1, waiter, &mutex);

	/* Give lowprio thread 1 more ms to block on the mutex */
	ms_sleep(6);

	dispatch("lock_stealing mutex_unlock 1", MUTEX_UNLOCK, 1, 0, &mutex);

	/* Try to stealing the lock from low prio task */
	trylock_result = dispatch("lock_stealing mutex_trylock",
				  MUTEX_TRYLOCK, 0, 0, &mutex);
	if (trylock_result == 0) {
		ms_sleep(6);

		dispatch("lock_stealing mutex_unlock 2", MUTEX_UNLOCK, 1, 0,
			 &mutex);

		/* Let waiter_lowprio a chance to run */
		ms_sleep(20);

		dispatch("lock_stealing mutex_lock 3", MUTEX_LOCK, 1, 0, &mutex);

		/* Restart the waiter */
		dispatch("lock_stealing thread_create 2", THREAD_CREATE, 1, 0,
			 &lowprio_tid, 1, waiter, &mutex);

		ms_sleep(6);

		dispatch("lock_stealing mutex_unlock 3", MUTEX_UNLOCK, 1, 0, &mutex);
	} else if (trylock_result != EBUSY) {
		fprintf(stderr,
			"FAILURE: lock_stealing mutex_trylock: %i (%s)\n",
			trylock_result, strerror(trylock_result));
		exit(EXIT_FAILURE);
	}

	/* Stealing the lock (again) from low prio task */
	dispatch("lock_stealing mutex_lock 4", MUTEX_LOCK, 1, 0, &mutex);

	ms_sleep(6);

	dispatch("lock_stealing mutex_unlock 4", MUTEX_UNLOCK, 1, 0, &mutex);

	/* Let waiter_lowprio a chance to run */
	ms_sleep(20);

	dispatch("lock_stealing mutex_destroy", MUTEX_DESTROY, 1, 0, &mutex);

	if (trylock_result != 0)
		smokey_note("mutex_trylock not supported");
}

static void *victim(void *cookie)
{
	pthread_mutex_t *mutex = (pthread_mutex_t *) cookie;
	unsigned long long start;

	dispatch("victim pthread_detach", THREAD_DETACH, 1, 0);
	dispatch("victim mutex_lock", MUTEX_LOCK, 1, 0, mutex);

	start = timer_get_tsc();
	while (timer_tsc2ns(timer_get_tsc() - start) < 110000000);

	dispatch("victim mutex_unlock", MUTEX_UNLOCK, 1, 0, mutex);

	return cookie;
}

static void deny_stealing(void)
{
	unsigned long long start, diff;
	pthread_mutex_t mutex;
	pthread_t lowprio_tid;

	smokey_trace("%s", __func__);

	dispatch("deny_stealing mutex_init", MUTEX_CREATE, 1, 0, &mutex,
		 PTHREAD_PRIO_INHERIT, PTHREAD_MUTEX_NORMAL);
	dispatch("deny_stealing mutex_lock 1", MUTEX_LOCK, 1, 0, &mutex);

	/* Main thread should have higher priority */
	dispatch("deny_stealing thread_create", THREAD_CREATE, 1, 0,
		 &lowprio_tid, 1, victim, &mutex);

	/* Give lowprio thread 1 more ms to block on the mutex */
	ms_sleep(6);

	dispatch("deny_stealing mutex_unlock 1", MUTEX_UNLOCK, 1, 0, &mutex);

	/* Steal the lock for a short while */
	dispatch("deny_stealing mutex_lock 2", MUTEX_LOCK, 1, 0, &mutex);
	dispatch("deny_stealing mutex_unlock 2", MUTEX_UNLOCK, 1, 0, &mutex);

	/* Give lowprio thread a chance to run */
	ms_sleep(6);

	/* Try to reacquire the lock, but the lowprio thread should hold it */
	start = timer_get_tsc();
	dispatch("deny_stealing mutex_lock 3", MUTEX_LOCK, 1, 0, &mutex);
	diff = timer_tsc2ns(timer_get_tsc() - start);
	if (diff < 10000000) {
		fprintf(stderr, "FAILURE: main, waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}

	dispatch("deny_stealing mutex_unlock 3", MUTEX_UNLOCK, 1, 0, &mutex);

	/* Let waiter_lowprio a chance to run */
	ms_sleep(20);

	dispatch("deny_stealing mutex_destroy", MUTEX_DESTROY, 1, 0, &mutex);
}

struct cond_mutex {
	pthread_mutex_t *mutex;
	pthread_cond_t *cond;
};

static void *cond_signaler(void *cookie)
{
	struct cond_mutex *cm = (struct cond_mutex *) cookie;
	unsigned long long start, diff;

	start = timer_get_tsc();
	dispatch("cond_signaler mutex_lock 1", MUTEX_LOCK, 1, 0, cm->mutex);
	diff = timer_tsc2ns(timer_get_tsc() - start);

	if (diff < 10000000) {
		fprintf(stderr,
			"FAILURE: cond_signaler, mutex_lock waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}
	ms_sleep(11);
	dispatch("cond_signaler cond_signal", COND_SIGNAL, 1, 0, cm->cond);
	dispatch("cond_signaler mutex_unlock 2", MUTEX_UNLOCK, 1, 0, cm->mutex);
	sched_yield();

	start = timer_get_tsc();
	dispatch("cond_signaler mutex_lock 2", MUTEX_LOCK, 1, 0, cm->mutex);
	diff = timer_tsc2ns(timer_get_tsc() - start);
	if (diff < 10000000) {
		fprintf(stderr,
			"FAILURE: cond_signaler, mutex_lock 2 waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}
	dispatch("cond_signaler mutex_unlock 2", MUTEX_UNLOCK, 1, 0, cm->mutex);

	return cookie;
}

static void simple_condwait(void)
{
	unsigned long long start, diff;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	struct cond_mutex cm = {
		.mutex = &mutex,
		.cond = &cond,
	};
	pthread_t cond_signaler_tid;

	smokey_trace("%s", __func__);

	dispatch("simple_condwait mutex_init", MUTEX_CREATE, 1, 0, &mutex,
		 PTHREAD_PRIO_NONE, PTHREAD_MUTEX_NORMAL);
	dispatch("simple_condwait cond_init", COND_CREATE, 1, 0, &cond);
	dispatch("simple_condwait mutex_lock", MUTEX_LOCK, 1, 0, &mutex);
	dispatch("simple_condwait thread_create", THREAD_CREATE, 1, 0,
		 &cond_signaler_tid, 2, cond_signaler, &cm);

	ms_sleep(11);
	start = timer_get_tsc();
	dispatch("simple_condwait cond_wait", COND_WAIT, 1, 0, &cond, &mutex);
	diff = timer_tsc2ns(timer_get_tsc() - start);
	if (diff < 10000000) {
		fprintf(stderr, "FAILURE: main, waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}
	ms_sleep(11);
	dispatch("simple_condwait mutex_unlock", MUTEX_UNLOCK, 1, 0, &mutex);
	sched_yield();

	dispatch("simple_condwait mutex_destroy", MUTEX_DESTROY, 1, 0, &mutex);
	dispatch("simple_condwait cond_destroy", COND_DESTROY, 1, 0, &cond);

	dispatch("simple_condwait join", THREAD_JOIN, 1, 0, &cond_signaler_tid);
}

static void recursive_condwait(void)
{
	unsigned long long start, diff;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	struct cond_mutex cm = {
		.mutex = &mutex,
		.cond = &cond,
	};
	pthread_t cond_signaler_tid;

	smokey_trace("%s", __func__);

	dispatch("rec_condwait mutex_init", MUTEX_CREATE, 1, 0, &mutex,
		 PTHREAD_PRIO_NONE, PTHREAD_MUTEX_RECURSIVE);
	dispatch("rec_condwait cond_init", COND_CREATE, 1, 0, &cond);
	dispatch("rec_condwait mutex_lock 1", MUTEX_LOCK, 1, 0, &mutex);
	dispatch("rec_condwait mutex_lock 2", MUTEX_LOCK, 1, 0, &mutex);
	dispatch("rec_condwait thread_create", THREAD_CREATE, 1, 0,
		 &cond_signaler_tid, 2, cond_signaler, &cm);

	ms_sleep(11);
	start = timer_get_tsc();
	dispatch("rec_condwait cond_wait", COND_WAIT, 1, 0, &cond, &mutex);
	diff = timer_tsc2ns(timer_get_tsc() - start);
	if (diff < 10000000) {
		fprintf(stderr, "FAILURE: main, waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}
	dispatch("rec_condwait mutex_unlock 1", MUTEX_UNLOCK, 1, 0, &mutex);
	ms_sleep(11);
	dispatch("rec_condwait mutex_unlock 2", MUTEX_UNLOCK, 1, 0, &mutex);
	sched_yield();

	dispatch("rec_condwait mutex_destroy", MUTEX_DESTROY, 1, 0, &mutex);
	dispatch("rec_condwait cond_destroy", COND_DESTROY, 1, 0, &cond);

	dispatch("rec_condwait join", THREAD_JOIN, 1, 0, &cond_signaler_tid);
}

static void nrt_lock(void *cookie)
{
	pthread_mutex_t *mutex = cookie;

	/* Check that XNWEAK flag gets cleared and set back when
	   changing priority */
	check_current_mode(XNRELAX | XNWEAK, XNRELAX | XNWEAK);
	check_current_prio(0);
	dispatch("auto_switchback renice 1", THREAD_RENICE, 1, 0, 1);
	check_current_mode(XNWEAK, 0);
	check_current_prio(1);
	dispatch("auto_switchback renice 2", THREAD_RENICE, 1, 0, 0);
	check_current_mode(XNRELAX | XNWEAK, XNRELAX | XNWEAK);
	check_current_prio(0);

	/* Check mode changes for auto-switchback threads while using
	   mutexes with priority inheritance */
	dispatch("auto_switchback mutex_lock 1", MUTEX_LOCK, 1, 0, mutex);
	check_current_mode(XNRELAX, 0);
	ms_sleep(11);
	check_current_prio(2);
	dispatch("auto_switchback mutex_unlock 1", MUTEX_UNLOCK, 1, 0, mutex);
	check_current_mode(XNRELAX | XNWEAK, XNRELAX | XNWEAK);
}

static void auto_switchback(void)
{
	pthread_t nrt_lock_tid;
	pthread_mutex_t mutex;

	smokey_trace("%s", __func__);

	dispatch("auto_switchback mutex_init", MUTEX_CREATE, 1, 0, &mutex,
		 PTHREAD_PRIO_INHERIT, PTHREAD_MUTEX_RECURSIVE);
	dispatch("auto_switchback nrt thread_create", THREAD_CREATE, 1, 0,
		 &nrt_lock_tid, 0, nrt_lock, &mutex);
	ms_sleep(11);
	dispatch("auto_switchback mutex_lock 2", MUTEX_LOCK, 1, 0, &mutex);
	dispatch("auto_switchback mutex_unlock 2", MUTEX_UNLOCK, 1, 0, &mutex);

	dispatch("auto_switchback join", THREAD_JOIN, 1, 0, &nrt_lock_tid);
	dispatch("auto_switchback mutex_destroy", MUTEX_DESTROY, 1, 0, &mutex);
}

int run_posix_mutex(struct smokey_test *t, int argc, char *const argv[])
{
	struct sched_param sparam;
	struct sigaction sa;

	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = sigdebug;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGDEBUG, &sa, NULL);

	/* Set scheduling parameters for the current process */
	sparam.sched_priority = 2;
	pthread_setschedparam(pthread_self(), SCHED_FIFO, &sparam);

	/* Call test routines */
	autoinit_simple_wait();
	simple_wait();
	autoinit_recursive_wait();
	recursive_wait();
	autoinit_errorcheck_wait();
	errorcheck_wait();
	timed_mutex();
	mode_switch();
	pi_wait();
	lock_stealing();
	deny_stealing();
	simple_condwait();
	recursive_condwait();
	auto_switchback();

	return 0;
}

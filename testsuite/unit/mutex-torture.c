/*
 * Functional testing of the mutex implementation for native & posix skins.
 *
 * Copyright (C) Gilles Chanteperdrix  <gilles.chanteperdrix@xenomai.org>,
 *               Marion Deveaud <marion.deveaud@siemens.com>,
 *               Jan Kiszka <jan.kiszka@siemens.com>
 *
 * Released under the terms of GPLv2.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>

#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <pthread.h>
#ifndef __UCLIBC__
#include <execinfo.h>
#endif /* !__UCLIBC__ */

#include <native/timer.h>
#ifndef XENO_POSIX
#include <native/task.h>
#include <native/mutex.h>
#include <native/sem.h>
#include <native/cond.h>
#endif /* __NATIVE_SKIN */
#include <asm-generic/xenomai/bits/current.h> /* For internal use, do not use
					 in your code. */
#include <asm-generic/xenomai/stack.h>

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

#ifdef XENO_POSIX
typedef pthread_mutex_t mutex_t;
typedef	pthread_t thread_t;
typedef pthread_cond_t cond_t;
#else /* __NATIVE_SKIN__ */
typedef RT_MUTEX mutex_t;
typedef RT_TASK thread_t;
typedef RT_COND cond_t;
#endif /* __NATIVE_SKIN__ */

static const char *reason_str[] = {
	[SIGDEBUG_UNDEFINED] = "undefined",
	[SIGDEBUG_MIGRATE_SIGNAL] = "received signal",
	[SIGDEBUG_MIGRATE_SYSCALL] = "invoked syscall",
	[SIGDEBUG_MIGRATE_FAULT] = "triggered fault",
	[SIGDEBUG_MIGRATE_PRIOINV] = "affected by priority inversion",
	[SIGDEBUG_NOMLOCK] = "missing mlockall",
	[SIGDEBUG_WATCHDOG] = "runaway thread",
};

void sigdebug(int sig, siginfo_t *si, void *context)
{
	unsigned int reason = si->si_value.sival_int;
	void *bt[32];
	int nentries;

	printf("\nSIGDEBUG received, reason %d: %s\n", reason,
	       reason <= SIGDEBUG_WATCHDOG ? reason_str[reason] : "<unknown>");
#ifndef __UCLIBC__
	/* Dump a backtrace of the frame which caused the switch to
	   secondary mode: */
	nentries = backtrace(bt,sizeof(bt) / sizeof(bt[0]));
	backtrace_symbols_fd(bt,nentries,fileno(stdout));
#endif /* !__UCLIBC__ */
}

void timespec_add(struct timespec *ts, unsigned long long value)
{
	ts->tv_sec += value / 1000000000;
	ts->tv_nsec += value % 1000000000;
	if (ts->tv_nsec > 1000000000) {
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000;
	}
}

void ms_sleep(int time)
{
#ifdef XENO_POSIX
	struct timespec ts;

	ts.tv_sec = 0;
	ts.tv_nsec = time*NS_PER_MS;

	nanosleep(&ts, NULL);
#else /* __NATIVE_SKIN__ */
	rt_task_sleep(time*NS_PER_MS);
#endif /* __NATIVE_SKIN__ */
}

void check_current_prio(int expected_prio)
{
	int current_prio;
#ifdef XENO_POSIX
# ifdef __pse51_get_current_prio
	extern unsigned __pse51_muxid;

	XENOMAI_SKINCALL1(__pse51_muxid, __pse51_get_current_prio, &current_prio);
# else /* !__pse51_get_current_prio */
	current_prio = expected_prio;
# endif /* !__pse51_get_current_prio */

#else /* __NATIVE_SKIN__ */
	int ret;
	RT_TASK_INFO task_info;

	if ((ret = rt_task_inquire(NULL, &task_info)) < 0) {
		fprintf(stderr,
			"FAILURE: Task inquire: %i (%s)\n", -ret, strerror(-ret));
		exit(EXIT_FAILURE);
	}
	current_prio = task_info.cprio;
#endif /* __NATIVE_SKIN__ */

	if (current_prio != expected_prio) {
		fprintf(stderr,
			"FAILURE: current prio (%d) != expected prio (%d)\n",
			current_prio, expected_prio);
		exit(EXIT_FAILURE);
	}
}

void check_current_mode(int mask, int expected_value)
{
	int current_mode;

	/* This is a unit test, and in this circonstance, we are allowed to
	   call xeno_get_current_mode. But please do not do that in your
	   own code. */
	current_mode = xeno_get_current_mode() & mask;

	if (current_mode != expected_value) {
		fprintf(stderr,
			"FAILURE: current mode (%x) != expected mode (%x)\n",
			current_mode, expected_value);
		exit(EXIT_FAILURE);
	}
}

void yield(void)
{
#ifdef XENO_POSIX
	sched_yield();
#else /* __NATIVE_SKIN__ */
	rt_task_yield();
#endif /* __NATIVE_SKIN__ */
}

int dispatch(const char *service_name,
	     int service_type, int check, int expected, ...)
{
	unsigned long long timeout;
	thread_t *thread;
	cond_t *cond;
	void *handler;
	va_list ap;
	int status;
	mutex_t *mutex;
#ifdef XENO_POSIX
	struct sched_param param;
	pthread_attr_t threadattr;
	pthread_mutexattr_t mutexattr;
	struct timespec ts;
#else /* __NATIVE_SKIN__ */
	int prio;
#endif /* __NATIVE_SKIN__ */

	va_start(ap, expected);
	switch (service_type) {
	case MUTEX_CREATE:
#ifdef XENO_POSIX
		mutex = va_arg(ap, pthread_mutex_t *);
		pthread_mutexattr_init(&mutexattr);
#ifdef HAVE_PTHREAD_MUTEXATTR_SETPROTOCOL
		if (va_arg(ap, int) != 0)
			pthread_mutexattr_setprotocol(&mutexattr,
						      PTHREAD_PRIO_INHERIT);
#else
		status = va_arg(ap, int);
#endif
		pthread_mutexattr_settype(&mutexattr, va_arg(ap, int));
		status = pthread_mutex_init(mutex, &mutexattr);
#else /* __NATIVE_SKIN__ */
		status = -rt_mutex_create(va_arg(ap, RT_MUTEX *), NULL);
#endif /* __NATIVE_SKIN__ */
		break;

	case MUTEX_LOCK:
#ifdef XENO_POSIX
		status = pthread_mutex_lock(va_arg(ap, pthread_mutex_t *));
#else /* __NATIVE_SKIN__ */
		status =
		    -rt_mutex_acquire(va_arg(ap, RT_MUTEX *), TM_INFINITE);
#endif /* __NATIVE_SKIN__ */
		break;

	case MUTEX_TRYLOCK:
#ifdef XENO_POSIX
		status = pthread_mutex_trylock(va_arg(ap, pthread_mutex_t *));
#else /* __NATIVE_SKIN__ */
		status =
		    -rt_mutex_acquire(va_arg(ap, RT_MUTEX *), TM_NONBLOCK);
#endif /* __NATIVE_SKIN__ */
		break;

	case MUTEX_TIMED_LOCK:
		mutex = va_arg(ap, mutex_t *);
		timeout = va_arg(ap, unsigned long long);
#ifdef XENO_POSIX
		clock_gettime(CLOCK_REALTIME, &ts);
		timespec_add(&ts, timeout);
		status = pthread_mutex_timedlock(mutex, &ts);
#else /* __NATIVE_SKIN__ */
		status = -rt_mutex_acquire(mutex, timeout);
#endif /* __NATIVE_SKIN__ */
		break;

	case MUTEX_UNLOCK:
#ifdef XENO_POSIX
		status = pthread_mutex_unlock(va_arg(ap, pthread_mutex_t *));
#else /* __NATIVE_SKIN__ */
		status = -rt_mutex_release(va_arg(ap, RT_MUTEX *));
#endif /* __NATIVE_SKIN__ */
		break;

	case MUTEX_DESTROY:
#ifdef XENO_POSIX
		status = pthread_mutex_destroy(va_arg(ap, pthread_mutex_t *));
#else /* __NATIVE_SKIN__ */
		status = -rt_mutex_delete(va_arg(ap, RT_MUTEX *));
#endif /* __NATIVE_SKIN__ */
		break;

	case COND_CREATE:
#ifdef XENO_POSIX
		status = pthread_cond_init(va_arg(ap, pthread_cond_t *), NULL);
#else /* __NATIVE_SKIN__ */
		status = -rt_cond_create(va_arg(ap, RT_COND *), NULL);
#endif /* __NATIVE_SKIN__ */
		break;

	case COND_SIGNAL:
#ifdef XENO_POSIX
		status = pthread_cond_signal(va_arg(ap, pthread_cond_t *));
#else /* __NATIVE_SKIN__ */
		status = -rt_cond_signal(va_arg(ap, RT_COND *));
#endif /* __NATIVE_SKIN__ */
		break;

	case COND_WAIT:
#ifdef XENO_POSIX
		cond = va_arg(ap, pthread_cond_t *);
		status =
		    pthread_cond_wait(cond, va_arg(ap, pthread_mutex_t *));
#else /* __NATIVE_SKIN__ */
		cond = va_arg(ap, RT_COND *);
		status =
		    -rt_cond_wait(cond, va_arg(ap, RT_MUTEX *), TM_INFINITE);
#endif /* __NATIVE_SKIN__ */
		break;

	case COND_DESTROY:
#ifdef XENO_POSIX
		status = pthread_cond_destroy(va_arg(ap, pthread_cond_t *));
#else /* __NATIVE_SKIN__ */
		status = -rt_cond_delete(va_arg(ap, RT_COND *));
#endif /* __NATIVE_SKIN__ */
		break;

#ifdef XENO_POSIX
	case THREAD_DETACH:
		status = pthread_detach(pthread_self());
		break;
#else /* __NATIVE_SKIN__ */
	case THREAD_DETACH:
		return 0;
#endif /* __NATIVE_SKIN__ */

	case THREAD_CREATE:
#ifdef XENO_POSIX
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
		pthread_attr_setstacksize(&threadattr, xeno_stacksize(0));
		handler = va_arg(ap, void *);
		status = pthread_create(thread, &threadattr, handler,
					va_arg(ap, void *));
#else /* __NATIVE_SKIN__ */
		thread = va_arg(ap, RT_TASK *);
		prio = va_arg(ap, int);
		handler = va_arg(ap, void *);
		status = -rt_task_spawn(thread, NULL, 0, prio, T_JOINABLE, handler,
					va_arg(ap, void *));
#endif /* __NATIVE_SKIN__ */
		break;

	case THREAD_JOIN:
#ifdef XENO_POSIX
		thread = va_arg(ap, pthread_t *);
		status = pthread_join(*thread, NULL);
#else /* __NATIVE_SKIN__ */
		thread = va_arg(ap, RT_TASK *);
		status = rt_task_join(thread);
#endif /* __NATIVE_SKIN__ */
		break;

	case THREAD_RENICE:
#ifdef XENO_POSIX
		param.sched_priority = va_arg(ap, int);
		if (param.sched_priority)
			status = pthread_setschedparam(pthread_self(),
						       SCHED_FIFO, &param);
		else
			status = pthread_setschedparam(pthread_self(),
						       SCHED_OTHER, &param);
#else /* __NATIVE_SKIN__ */
		prio = va_arg(ap, int);
		status = -rt_task_set_priority(rt_task_self(), prio);
		if (status < 0)
			status = 0;
#endif /* __NATIVE_SKIN__ */
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

void *waiter(void *cookie)
{
	mutex_t *mutex = (mutex_t *) cookie;
	unsigned long long start, diff;

	dispatch("waiter pthread_detach", THREAD_DETACH, 1, 0);
	start = rt_timer_tsc();
	dispatch("waiter mutex_lock", MUTEX_LOCK, 1, 0, mutex);
	diff = rt_timer_tsc2ns(rt_timer_tsc() - start);
	if (diff < 10000000) {
		fprintf(stderr, "FAILURE: waiter, waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}
	ms_sleep(11);
	dispatch("waiter mutex_unlock", MUTEX_UNLOCK, 1, 0, mutex);

	return cookie;
}

void simple_wait(void)
{
	unsigned long long start, diff;
	mutex_t mutex;
	thread_t waiter_tid;

	fprintf(stderr, "simple_wait\n");

	dispatch("simple mutex_init", MUTEX_CREATE, 1, 0, &mutex, 0, 0);
	dispatch("simple mutex_lock 1", MUTEX_LOCK, 1, 0, &mutex);
	dispatch("simple thread_create", THREAD_CREATE, 1, 0, &waiter_tid, 2,
		 waiter, &mutex);
	ms_sleep(11);
	dispatch("simple mutex_unlock 1", MUTEX_UNLOCK, 1, 0, &mutex);
	yield();

	start = rt_timer_tsc();
	dispatch("simple mutex_lock 2", MUTEX_LOCK, 1, 0, &mutex);
	diff = rt_timer_tsc2ns(rt_timer_tsc() - start);
	if (diff < 10000000) {
		fprintf(stderr, "FAILURE: main, waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}

	dispatch("simple mutex_unlock 2", MUTEX_UNLOCK, 1, 0, &mutex);
	dispatch("simple mutex_destroy", MUTEX_DESTROY, 1, 0, &mutex);
}

void recursive_wait(void)
{
	unsigned long long start, diff;
	mutex_t mutex;
	thread_t waiter_tid;

	fprintf(stderr, "recursive_wait\n");

	dispatch("rec mutex_init", MUTEX_CREATE, 1, 0, &mutex, 0,
		 PTHREAD_MUTEX_RECURSIVE);
	dispatch("rec mutex_lock 1", MUTEX_LOCK, 1, 0, &mutex);
	dispatch("rec mutex_lock 2", MUTEX_LOCK, 1, 0, &mutex);

	dispatch("rec thread_create", THREAD_CREATE, 1, 0, &waiter_tid, 2,
		 waiter, &mutex);

	dispatch("rec mutex_unlock 2", MUTEX_UNLOCK, 1, 0, &mutex);
	ms_sleep(11);
	dispatch("rec mutex_unlock 1", MUTEX_UNLOCK, 1, 0, &mutex);
	yield();

	start = rt_timer_tsc();
	dispatch("rec mutex_lock 3", MUTEX_LOCK, 1, 0, &mutex);
	diff = rt_timer_tsc2ns(rt_timer_tsc() - start);

	if (diff < 10000000) {
		fprintf(stderr, "FAILURE: main, waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}
	dispatch("rec mutex_unlock 3", MUTEX_UNLOCK, 1, 0, &mutex);
	dispatch("rec mutex_destroy", MUTEX_DESTROY, 1, 0, &mutex);
}

void errorcheck_wait(void)
{
#ifdef XENO_POSIX
	/* This test only makes sense under POSIX */
	unsigned long long start, diff;
	mutex_t mutex;
	thread_t waiter_tid;
	int err;

	fprintf(stderr, "errorcheck_wait\n");

	dispatch("errorcheck mutex_init", MUTEX_CREATE, 1, 0, &mutex, 0,
		 PTHREAD_MUTEX_ERRORCHECK);
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
	yield();
	err = pthread_mutex_unlock(&mutex);
	if (err != EPERM) {
		fprintf(stderr, "FAILURE: errorcheck mutex_unlock 2: %s\n",
			strerror(err));
		exit(EXIT_FAILURE);
	}

	start = rt_timer_tsc();
	dispatch("errorcheck mutex_lock 3", MUTEX_LOCK, 1, 0, &mutex);
	diff = rt_timer_tsc2ns(rt_timer_tsc() - start);
	if (diff < 10000000) {
		fprintf(stderr, "FAILURE: main, waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}
	dispatch("errorcheck mutex_unlock 3", MUTEX_UNLOCK, 1, 0, &mutex);
	dispatch("errorcheck mutex_destroy", MUTEX_DESTROY, 1, 0, &mutex);
#endif /* XENO_POSIX */
}

void *timed_waiter(void *cookie)
{
	mutex_t *mutex = (mutex_t *) cookie;
	unsigned long long start, diff;

	dispatch("timed_waiter pthread_detach", THREAD_DETACH, 1, 0);

	start = rt_timer_tsc();
	dispatch("timed_waiter mutex_timed_lock", MUTEX_TIMED_LOCK, 1,
		 ETIMEDOUT, mutex, 10000000ULL);
	diff = rt_timer_tsc2ns(rt_timer_tsc() - start);
	if (diff < 10000000) {
		fprintf(stderr, "FAILURE: timed_waiter, waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}

	return cookie;
}

void timed_mutex(void)
{
	mutex_t mutex;
	thread_t waiter_tid;

	fprintf(stderr, "timed_mutex\n");

	dispatch("timed_mutex mutex_init", MUTEX_CREATE, 1, 0, &mutex, 1, 0);
	dispatch("timed_mutex mutex_lock 1", MUTEX_LOCK, 1, 0, &mutex);
	dispatch("timed_mutex thread_create", THREAD_CREATE, 1, 0, &waiter_tid,
		 2, timed_waiter, &mutex);
	ms_sleep(20);
	dispatch("timed_mutex mutex_unlock 1", MUTEX_UNLOCK, 1, 0, &mutex);
	ms_sleep(11);
	dispatch("timed_mutex mutex_destroy", MUTEX_DESTROY, 1, 0, &mutex);

}

void mode_switch(void)
{
	mutex_t mutex;

	/* Cause a switch to secondary mode */
#ifdef XENO_POSIX
	__real_sched_yield();
#endif
	fprintf(stderr, "mode_switch\n");

	dispatch("switch mutex_init", MUTEX_CREATE, 1, 0, &mutex, 1, 0);

	check_current_mode(XNRELAX, XNRELAX);

	dispatch("switch mutex_lock", MUTEX_LOCK, 1, 0, &mutex);

	check_current_mode(XNRELAX, 0);

	dispatch("switch mutex_unlock", MUTEX_UNLOCK, 1, 0, &mutex);
	dispatch("switch mutex_destroy", MUTEX_DESTROY, 1, 0, &mutex);
}

void pi_wait(void)
{
	unsigned long long start, diff;
	mutex_t mutex;
	thread_t waiter_tid;

	fprintf(stderr, "pi_wait\n");

	dispatch("pi mutex_init", MUTEX_CREATE, 1, 0, &mutex, 1, 0);
	dispatch("pi mutex_lock 1", MUTEX_LOCK, 1, 0, &mutex);

	check_current_prio(2);

	/* Give waiter a higher priority than main thread */
	dispatch("pi thread_create", THREAD_CREATE, 1, 0, &waiter_tid, 3, waiter,
		 &mutex);
	ms_sleep(11);

	check_current_prio(3);

	dispatch("pi mutex_unlock 1", MUTEX_UNLOCK, 1, 0, &mutex);
	yield();

	check_current_prio(2);

	start = rt_timer_tsc();
	dispatch("pi mutex_lock 2", MUTEX_LOCK, 1, 0, &mutex);
	diff = rt_timer_tsc2ns(rt_timer_tsc() - start);
	if (diff < 10000000) {
		fprintf(stderr, "FAILURE: main, waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}
	dispatch("pi mutex_unlock 2", MUTEX_UNLOCK, 1, 0, &mutex);
	dispatch("pi mutex_destroy", MUTEX_DESTROY, 1, 0, &mutex);
}

void lock_stealing(void)
{
	mutex_t mutex;
	thread_t lowprio_tid;
	int trylock_result;

	/* Main thread acquires the mutex and starts a waiter with lower
	   priority. Then main thread releases the mutex, but locks it again
	   without giving the waiter a chance to get it beforehand. */

	fprintf(stderr, "lock_stealing\n");

	dispatch("lock_stealing mutex_init", MUTEX_CREATE, 1, 0, &mutex, 1, 0);
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
#ifdef XENO_POSIX
	} else if (trylock_result != EBUSY) {
#else /* __NATIVE_SKIN__ */
	} else if (trylock_result != EWOULDBLOCK) {
#endif /* __NATIVE_SKIN__ */
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
		fprintf(stderr,
			"NOTE: lock_stealing mutex_trylock: not supported\n");
}

void *victim(void *cookie)
{
	mutex_t *mutex = (mutex_t *) cookie;
	unsigned long long start;

	dispatch("victim pthread_detach", THREAD_DETACH, 1, 0);
	dispatch("victim mutex_lock", MUTEX_LOCK, 1, 0, mutex);

	start = rt_timer_tsc();
	while (rt_timer_tsc2ns(rt_timer_tsc() - start) < 110000000);

	dispatch("victim mutex_unlock", MUTEX_UNLOCK, 1, 0, mutex);

	return cookie;
}

void deny_stealing(void)
{
	unsigned long long start, diff;
	mutex_t mutex;
	thread_t lowprio_tid;

	fprintf(stderr, "deny_stealing\n");

	dispatch("deny_stealing mutex_init", MUTEX_CREATE, 1, 0, &mutex, 1, 0);
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
	start = rt_timer_tsc();
	dispatch("deny_stealing mutex_lock 3", MUTEX_LOCK, 1, 0, &mutex);
	diff = rt_timer_tsc2ns(rt_timer_tsc() - start);
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
	mutex_t *mutex;
	cond_t *cond;
};

void *cond_signaler(void *cookie)
{
	struct cond_mutex *cm = (struct cond_mutex *) cookie;
	unsigned long long start, diff;

	start = rt_timer_tsc();
	dispatch("cond_signaler mutex_lock 1", MUTEX_LOCK, 1, 0, cm->mutex);
	diff = rt_timer_tsc2ns(rt_timer_tsc() - start);

	if (diff < 10000000) {
		fprintf(stderr,
			"FAILURE: cond_signaler, mutex_lock waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}
	ms_sleep(11);
	dispatch("cond_signaler cond_signal", COND_SIGNAL, 1, 0, cm->cond);
	dispatch("cond_signaler mutex_unlock 2", MUTEX_UNLOCK, 1, 0, cm->mutex);
	yield();

	start = rt_timer_tsc();
	dispatch("cond_signaler mutex_lock 2", MUTEX_LOCK, 1, 0, cm->mutex);
	diff = rt_timer_tsc2ns(rt_timer_tsc() - start);
	if (diff < 10000000) {
		fprintf(stderr,
			"FAILURE: cond_signaler, mutex_lock 2 waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}
	dispatch("cond_signaler mutex_unlock 2", MUTEX_UNLOCK, 1, 0, cm->mutex);

	return cookie;
}

void simple_condwait(void)
{
	unsigned long long start, diff;
	mutex_t mutex;
	cond_t cond;
	struct cond_mutex cm = {
		.mutex = &mutex,
		.cond = &cond,
	};
	thread_t cond_signaler_tid;

	fprintf(stderr, "simple_condwait\n");

	dispatch("simple_condwait mutex_init", MUTEX_CREATE, 1, 0, &mutex);
	dispatch("simple_condwait cond_init", COND_CREATE, 1, 0, &cond);
	dispatch("simple_condwait mutex_lock", MUTEX_LOCK, 1, 0, &mutex);
	dispatch("simple_condwait thread_create", THREAD_CREATE, 1, 0,
		 &cond_signaler_tid, 2, cond_signaler, &cm);

	ms_sleep(11);
	start = rt_timer_tsc();
	dispatch("simple_condwait cond_wait", COND_WAIT, 1, 0, &cond, &mutex);
	diff = rt_timer_tsc2ns(rt_timer_tsc() - start);
	if (diff < 10000000) {
		fprintf(stderr, "FAILURE: main, waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}
	ms_sleep(11);
	dispatch("simple_condwait mutex_unlock", MUTEX_UNLOCK, 1, 0, &mutex);
	yield();

	dispatch("simple_condwait mutex_destroy", MUTEX_DESTROY, 1, 0, &mutex);
	dispatch("simple_condwait cond_destroy", COND_DESTROY, 1, 0, &cond);

	dispatch("simple_condwait join", THREAD_JOIN, 1, 0, &cond_signaler_tid);
}

void recursive_condwait(void)
{
	unsigned long long start, diff;
	mutex_t mutex;
	cond_t cond;
	struct cond_mutex cm = {
		.mutex = &mutex,
		.cond = &cond,
	};
	thread_t cond_signaler_tid;

	fprintf(stderr, "recursive_condwait\n");

	dispatch("rec_condwait mutex_init", MUTEX_CREATE, 1, 0, &mutex, 0,
		 PTHREAD_MUTEX_RECURSIVE);
	dispatch("rec_condwait cond_init", COND_CREATE, 1, 0, &cond);
	dispatch("rec_condwait mutex_lock 1", MUTEX_LOCK, 1, 0, &mutex);
	dispatch("rec_condwait mutex_lock 2", MUTEX_LOCK, 1, 0, &mutex);
	dispatch("rec_condwait thread_create", THREAD_CREATE, 1, 0,
		 &cond_signaler_tid, 2, cond_signaler, &cm);

	ms_sleep(11);
	start = rt_timer_tsc();
	dispatch("rec_condwait cond_wait", COND_WAIT, 1, 0, &cond, &mutex);
	diff = rt_timer_tsc2ns(rt_timer_tsc() - start);
	if (diff < 10000000) {
		fprintf(stderr, "FAILURE: main, waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}
	dispatch("rec_condwait mutex_unlock 1", MUTEX_UNLOCK, 1, 0, &mutex);
	ms_sleep(11);
	dispatch("rec_condwait mutex_unlock 2", MUTEX_UNLOCK, 1, 0, &mutex);
	yield();

	dispatch("rec_condwait mutex_destroy", MUTEX_DESTROY, 1, 0, &mutex);
	dispatch("rec_condwait cond_destroy", COND_DESTROY, 1, 0, &cond);

	dispatch("rec_condwait join", THREAD_JOIN, 1, 0, &cond_signaler_tid);
}

void nrt_lock(void *cookie)
{
	mutex_t *mutex = (mutex_t *)cookie;

	/* Check that XNOTHER flag gets cleared and set back when
	   changing priority */
	check_current_mode(XNRELAX | XNOTHER, XNRELAX | XNOTHER);
	check_current_prio(0);
	dispatch("auto_switchback renice 1", THREAD_RENICE, 1, 0, 1);
	check_current_mode(XNOTHER, 0);
	check_current_prio(1);
	dispatch("auto_switchback renice 2", THREAD_RENICE, 1, 0, 0);
	check_current_mode(XNRELAX | XNOTHER, XNRELAX | XNOTHER);
	check_current_prio(0);

	/* Check mode changes for auto-switchback threads while using
	   mutexes with priority inheritance */
	dispatch("auto_switchback mutex_lock 1", MUTEX_LOCK, 1, 0, mutex);
	check_current_mode(XNRELAX, 0);
	ms_sleep(11);
	check_current_prio(2);
	dispatch("auto_switchback mutex_unlock 1", MUTEX_UNLOCK, 1, 0, mutex);
	check_current_mode(XNRELAX | XNOTHER, XNRELAX | XNOTHER);
}

void auto_switchback(void)
{
	thread_t nrt_lock_tid;
	mutex_t mutex;

	fprintf(stderr, "auto_switchback\n");

	dispatch("auto_switchback mutex_init", MUTEX_CREATE, 1, 0, &mutex, 1,
		 PTHREAD_MUTEX_RECURSIVE);
	dispatch("auto_switchback nrt thread_create", THREAD_CREATE, 1, 0,
		 &nrt_lock_tid, 0, nrt_lock, &mutex);
	ms_sleep(11);
	dispatch("auto_switchback mutex_lock 2", MUTEX_LOCK, 1, 0, &mutex);
	dispatch("auto_switchback mutex_unlock 2", MUTEX_UNLOCK, 1, 0, &mutex);

	dispatch("auto_switchback join", THREAD_JOIN, 1, 0, &nrt_lock_tid);
	dispatch("auto_switchback mutex_destroy", MUTEX_DESTROY, 1, 0, &mutex);
}

int main(void)
{
#ifdef XENO_POSIX
	struct sched_param sparam;
#else /* __NATIVE_SKIN__ */
	RT_TASK main_tid;
#endif /* __NATIVE_SKIN__ */
	struct sigaction sa;

	mlockall(MCL_CURRENT | MCL_FUTURE);

	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = sigdebug;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGDEBUG, &sa, NULL);

	/* Set scheduling parameters for the current process */
#ifdef XENO_POSIX
	sparam.sched_priority = 2;
	pthread_setschedparam(pthread_self(), SCHED_FIFO, &sparam);
#else /* __NATIVE_SKIN__ */
	rt_task_shadow(&main_tid, "main_task", 2, 0);
#endif /* __NATIVE_SKIN__ */

	/* Call test routines */
	simple_wait();
	recursive_wait();
	errorcheck_wait();
	timed_mutex();
	mode_switch();
	pi_wait();
	lock_stealing();
	deny_stealing();
	simple_condwait();
	recursive_condwait();
	auto_switchback();
	fprintf(stderr, "Test OK\n");
	return 0;
}

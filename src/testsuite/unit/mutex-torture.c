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
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <pthread.h>
#include <native/timer.h>

#ifndef XENO_POSIX
#include <native/task.h>
#include <native/mutex.h>
#include <native/sem.h>
#include <native/cond.h>
#endif /* __NATIVE_SKIN */
#include <asm-generic/bits/current.h> /* For internal use, do not use
				         in your code. */

#define MUTEX_CREATE	1
#define MUTEX_LOCK	2
#define MUTEX_TRYLOCK	3
#define MUTEX_UNLOCK	4
#define MUTEX_DESTROY	5
#define COND_CREATE	6
#define COND_SIGNAL	7
#define COND_WAIT	8
#define COND_DESTROY	9
#define THREAD_DETACH	10
#define THREAD_CREATE	11

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
		fprintf(stderr, "Task inquire: %i (%s)\n", -ret, strerror(-ret));
		exit(EXIT_FAILURE);
	}
	current_prio = task_info.cprio;
#endif /* __NATIVE_SKIN__ */

	if (current_prio != expected_prio) {
		fprintf(stderr, "current prio (%d) != expected prio (%d)\n",
			current_prio, expected_prio);
		exit(EXIT_FAILURE);
	}
}

void check_current_mode(int expected_primary_mode)
{
	int current_in_primary;

        /* This is a unit test, and in this circonstance, we are allowed to
	   call xeno_get_current_mode. But please do not do that in your
	   own code. */
	current_in_primary = !(xeno_get_current_mode() & XNRELAX);
	
	if (current_in_primary != expected_primary_mode) {
		fprintf(stderr, "current mode (%d) != expected mode (%d)\n",
			current_in_primary, expected_primary_mode);
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

int dispatch(const char *service_name, int service_type, int check, ...)
{
	thread_t *thread;
	cond_t *cond;
	void *handler;
	va_list ap;
	int status;
#ifdef XENO_POSIX
	struct sched_param param;
	pthread_attr_t threadattr;
	pthread_mutexattr_t mutexattr;
	pthread_mutex_t *mutex;
#else /* __NATIVE_SKIN__ */
	int prio;
#endif /* __NATIVE_SKIN__ */

	va_start(ap, check);
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
		pthread_attr_setschedpolicy(&threadattr, SCHED_FIFO);
		param.sched_priority = va_arg(ap, int);
		pthread_attr_setschedparam(&threadattr, &param);
		pthread_attr_setinheritsched(&threadattr,
					     PTHREAD_EXPLICIT_SCHED);
		pthread_attr_setstacksize(&threadattr, 32768);
		handler = va_arg(ap, void *);
		status = pthread_create(thread, &threadattr, handler,
					va_arg(ap, void *));
#else /* __NATIVE_SKIN__ */
		thread = va_arg(ap, RT_TASK *);
		prio = va_arg(ap, int);
		handler = va_arg(ap, void *);
		status = -rt_task_spawn(thread, NULL, 0, prio, 0, handler,
					va_arg(ap, void *));
#endif /* __NATIVE_SKIN__ */
		break;

	default:
		fprintf(stderr, "Unknown service %i.\n", service_type);
		exit(EXIT_FAILURE);
	}
	va_end(ap);

	if (status > 0 && check) {
		fprintf(stderr, "%s: %i (%s)\n", 
			service_name, status, strerror(status));
		exit(EXIT_FAILURE);
	}
	return status;
}

void *waiter(void *cookie)
{
	mutex_t *mutex = (mutex_t *) cookie;
	unsigned long long start, diff;

	dispatch("waiter pthread_detach", THREAD_DETACH, 1);
	start = rt_timer_tsc();
	dispatch("waiter mutex_lock", MUTEX_LOCK, 1, mutex);
	diff = rt_timer_tsc2ns(rt_timer_tsc() - start);
	if (diff < 10000000) {
		fprintf(stderr, "waiter, waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}
	ms_sleep(11);
	dispatch("waiter mutex_unlock", MUTEX_UNLOCK, 1, mutex);

	return cookie;
}

void simple_wait(void)
{
	unsigned long long start, diff;
	mutex_t mutex;
	thread_t waiter_tid;

	fprintf(stderr, "simple_wait\n");

	dispatch("simple mutex_init", MUTEX_CREATE, 1, &mutex, 0, 0);
	dispatch("simple mutex_lock 1", MUTEX_LOCK, 1, &mutex);
	dispatch("simple thread_create", THREAD_CREATE, 1, &waiter_tid, 2,
		 waiter, &mutex);
	ms_sleep(10);
	dispatch("simple mutex_unlock 1", MUTEX_UNLOCK, 1, &mutex);
	yield();

	start = rt_timer_tsc();
	dispatch("simple mutex_lock 2", MUTEX_LOCK, 1, &mutex);
	diff = rt_timer_tsc2ns(rt_timer_tsc() - start);
	if (diff < 10000000) {
		fprintf(stderr, "main, waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}

	dispatch("simple mutex_unlock 2", MUTEX_UNLOCK, 1, &mutex);
	dispatch("simple mutex_destroy", MUTEX_DESTROY, 1, &mutex);
}

void recursive_wait(void)
{
	unsigned long long start, diff;
	mutex_t mutex;
	thread_t waiter_tid;

	fprintf(stderr, "recursive_wait\n");

	dispatch("rec mutex_init", MUTEX_CREATE, 1, &mutex, 0,
		 PTHREAD_MUTEX_RECURSIVE);
	dispatch("rec mutex_lock 1", MUTEX_LOCK, 1, &mutex);
	dispatch("rec mutex_lock 2", MUTEX_LOCK, 1, &mutex);

	dispatch("rec thread_create", THREAD_CREATE, 1, &waiter_tid, 2,
		 waiter, &mutex);

	dispatch("rec mutex_unlock 2", MUTEX_UNLOCK, 1, &mutex); 
	ms_sleep(10);
	dispatch("rec mutex_unlock 1", MUTEX_UNLOCK, 1, &mutex);
	yield();

	start = rt_timer_tsc();
	dispatch("rec mutex_lock 3", MUTEX_LOCK, 1, &mutex);
	diff = rt_timer_tsc2ns(rt_timer_tsc() - start);

	if (diff < 10000000) {
		fprintf(stderr, "main, waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}
	dispatch("rec mutex_unlock 3", MUTEX_UNLOCK, 1, &mutex);
	dispatch("rec mutex_destroy", MUTEX_DESTROY, 1, &mutex);
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

	dispatch("errorcheck mutex_init", MUTEX_CREATE, 1, &mutex, 0,
		 PTHREAD_MUTEX_ERRORCHECK);
	dispatch("errorcheck mutex_lock 1", MUTEX_LOCK, 1, &mutex);

	err = pthread_mutex_lock(&mutex);
	if (err != EDEADLK) {
		fprintf(stderr, "errorcheck mutex_lock 2: %s\n",
			strerror(err));
		exit(EXIT_FAILURE);
	}

	dispatch("errorcheck thread_create", THREAD_CREATE, 1, &waiter_tid, 2,
		 waiter, &mutex);
	ms_sleep(10);
	dispatch("errorcheck mutex_unlock 1", MUTEX_UNLOCK, 1, &mutex);
	yield();
	err = pthread_mutex_unlock(&mutex);
	if (err != EPERM) {
		fprintf(stderr, "errorcheck mutex_unlock 2: %s\n",
			strerror(err));
		exit(EXIT_FAILURE);
	}

	start = rt_timer_tsc();
	dispatch("errorcheck mutex_lock 3", MUTEX_LOCK, 1, &mutex);
	diff = rt_timer_tsc2ns(rt_timer_tsc() - start);
	if (diff < 10000000) {
		fprintf(stderr, "main, waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}
	dispatch("errorcheck mutex_unlock 3", MUTEX_UNLOCK, 1, &mutex);
	dispatch("errorcheck mutex_destroy", MUTEX_DESTROY, 1, &mutex);
#endif /* XENO_POSIX */
}

void mode_switch(void)
{
	mutex_t mutex;

	fprintf(stderr, "mode_switch\n");

	dispatch("switch mutex_init", MUTEX_CREATE, 1, &mutex, 1, 0);

	check_current_mode(0);

	dispatch("switch mutex_lock", MUTEX_LOCK, 1, &mutex);

	check_current_mode(1);

	dispatch("switch mutex_unlock", MUTEX_UNLOCK, 1, &mutex);
	dispatch("switch mutex_destroy", MUTEX_DESTROY, 1, &mutex);
}

void pi_wait(void)
{
	unsigned long long start, diff;
	mutex_t mutex;
	thread_t waiter_tid;

	fprintf(stderr, "pi_wait\n");

	dispatch("pi mutex_init", MUTEX_CREATE, 1, &mutex, 1, 0);
	dispatch("pi mutex_lock 1", MUTEX_LOCK, 1, &mutex);

	check_current_prio(2);

	/* Give waiter a higher priority than main thread */
	dispatch("pi thread_create", THREAD_CREATE, 1, &waiter_tid, 3, waiter,
		 &mutex);
	ms_sleep(10);

	check_current_prio(3);

	dispatch("pi mutex_unlock 1", MUTEX_UNLOCK, 1, &mutex);
	yield();

	check_current_prio(2);

	start = rt_timer_tsc();
	dispatch("pi mutex_lock 2", MUTEX_LOCK, 1, &mutex);
	diff = rt_timer_tsc2ns(rt_timer_tsc() - start);
	if (diff < 10000000) {
		fprintf(stderr, "main, waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}
	dispatch("pi mutex_unlock 2", MUTEX_UNLOCK, 1, &mutex);
	dispatch("pi mutex_destroy", MUTEX_DESTROY, 1, &mutex);
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

	dispatch("lock_stealing mutex_init", MUTEX_CREATE, 1, &mutex, 1, 0);
	dispatch("lock_stealing mutex_lock 1", MUTEX_LOCK, 1, &mutex);

	/* Main thread should have higher priority */
	dispatch("lock_stealing thread_create 1", THREAD_CREATE, 1,
		 &lowprio_tid, 1, waiter, &mutex);

	/* Give lowprio thread 1 more ms to block on the mutex */
	ms_sleep(6);

	dispatch("lock_stealing mutex_unlock 1", MUTEX_UNLOCK, 1, &mutex);

	/* Try to stealing the lock from low prio task */
	trylock_result = dispatch("lock_stealing mutex_trylock",
				  MUTEX_TRYLOCK, 0, &mutex);
	if (trylock_result == 0) {
		ms_sleep(6);

		dispatch("lock_stealing mutex_unlock 2", MUTEX_UNLOCK, 1,
			 &mutex);

		/* Let waiter_lowprio a chance to run */
		ms_sleep(20);

		dispatch("lock_stealing mutex_lock 3", MUTEX_LOCK, 1, &mutex);

		/* Restart the waiter */
		dispatch("lock_stealing thread_create 2", THREAD_CREATE, 1,
			 &lowprio_tid, 1, waiter, &mutex);

		ms_sleep(6);

		dispatch("lock_stealing mutex_unlock 3", MUTEX_UNLOCK, 1, &mutex);
#ifdef XENO_POSIX
	} else if (trylock_result != EBUSY) {
#else /* __NATIVE_SKIN__ */
	} else if (trylock_result != EWOULDBLOCK) {
#endif /* __NATIVE_SKIN__ */
		fprintf(stderr, "lock_stealing mutex_trylock: %i (%s)\n",
			trylock_result, strerror(trylock_result));
		exit(EXIT_FAILURE);
	}

	/* Stealing the lock (again) from low prio task */
	dispatch("lock_stealing mutex_lock 4", MUTEX_LOCK, 1, &mutex);

	ms_sleep(6);

	dispatch("lock_stealing mutex_unlock 4", MUTEX_UNLOCK, 1, &mutex);

	/* Let waiter_lowprio a chance to run */
	ms_sleep(20);

	dispatch("lock_stealing mutex_destroy", MUTEX_DESTROY, 1, &mutex);

	if (trylock_result != 0)
		fprintf(stderr,
			"lock_stealing mutex_trylock: not supported\n");
}

struct cond_mutex {
	mutex_t *mutex;
	cond_t *cond;
};

void *cond_signaler(void *cookie)
{
	struct cond_mutex *cm = (struct cond_mutex *) cookie;
	unsigned long long start, diff;

	dispatch("cond_signaler pthread_detach", THREAD_DETACH, 1);

	start = rt_timer_tsc();
	dispatch("cond_signaler mutex_lock 1", MUTEX_LOCK, 1, cm->mutex);
	diff = rt_timer_tsc2ns(rt_timer_tsc() - start);

	if (diff < 10000000) {
		fprintf(stderr,
			"cond_signaler, mutex_lock waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}
	ms_sleep(10);
	dispatch("cond_signaler cond_signal", COND_SIGNAL, 1, cm->cond);
	dispatch("cond_signaler mutex_unlock 2", MUTEX_UNLOCK, 1, cm->mutex);
	yield();

	start = rt_timer_tsc();
	dispatch("cond_signaler mutex_lock 2", MUTEX_LOCK, 1, cm->mutex);
	diff = rt_timer_tsc2ns(rt_timer_tsc() - start);
	if (diff < 10000000) {
		fprintf(stderr,
			"cond_signaler, mutex_lock 2 waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}
	dispatch("cond_signaler mutex_unlock 2", MUTEX_UNLOCK, 1, cm->mutex);

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

	dispatch("simple_condwait mutex_init", MUTEX_CREATE, 1, &mutex);
	dispatch("simple_condwait cond_init", COND_CREATE, 1, &cond);
	dispatch("simple_condwait mutex_lock", MUTEX_LOCK, 1, &mutex);
	dispatch("simple_condwait thread_create", THREAD_CREATE, 1,
		 &cond_signaler_tid, 2, cond_signaler, &cm);

	ms_sleep(11);
	start = rt_timer_tsc();
	dispatch("simple_condwait cond_wait", COND_WAIT, 1, &cond, &mutex);
	diff = rt_timer_tsc2ns(rt_timer_tsc() - start);
	if (diff < 10000000) {
		fprintf(stderr, "main, waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}
	ms_sleep(10);
	dispatch("simple_condwait mutex_unlock", MUTEX_UNLOCK, 1, &mutex);
	yield();

	dispatch("simple_condwait mutex_destroy", MUTEX_DESTROY, 1, &mutex);
	dispatch("simple_condwait cond_destroy", COND_DESTROY, 1, &cond);
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

	dispatch("rec_condwait mutex_init", MUTEX_CREATE, 1, &mutex, 0,
		 PTHREAD_MUTEX_RECURSIVE);
	dispatch("rec_condwait cond_init", COND_CREATE, 1, &cond);
	dispatch("rec_condwait mutex_lock 1", MUTEX_LOCK, 1, &mutex);
	dispatch("rec_condwait mutex_lock 2", MUTEX_LOCK, 1, &mutex);
	dispatch("rec_condwait thread_create", THREAD_CREATE, 1,
		 &cond_signaler_tid, 2, cond_signaler, &cm);

	ms_sleep(10);
	start = rt_timer_tsc();
	dispatch("rec_condwait cond_wait", COND_WAIT, 1, &cond, &mutex);
	diff = rt_timer_tsc2ns(rt_timer_tsc() - start);
	if (diff < 10000000) {
		fprintf(stderr, "main, waited %Ld.%03u us\n",
			diff / 1000, (unsigned) (diff % 1000));
		exit(EXIT_FAILURE);
	}
	dispatch("rec_condwait mutex_unlock 1", MUTEX_UNLOCK, 1, &mutex);
	ms_sleep(10);
	dispatch("rec_condwait mutex_unlock 2", MUTEX_UNLOCK, 1, &mutex);
	yield();

	dispatch("rec_condwait mutex_destroy", MUTEX_DESTROY, 1, &mutex);
	dispatch("rec_condwait cond_destroy", COND_DESTROY, 1, &cond);
}

int main(void)
{
#ifdef XENO_POSIX
	struct sched_param sparam;
#else /* __NATIVE_SKIN__ */
	RT_TASK main_tid;
#endif /* __NATIVE_SKIN__ */

	mlockall(MCL_CURRENT | MCL_FUTURE);

	/* Set scheduling parameters for the current process */
#ifdef XENO_POSIX
	sparam.sched_priority = 2;
	sched_setscheduler(0, SCHED_FIFO, &sparam);
#else /* __NATIVE_SKIN__ */
	rt_task_shadow(&main_tid, "main_task", 2, 0);
#endif /* __NATIVE_SKIN__ */

	/* Call test routines */
	simple_wait();
	recursive_wait();
	errorcheck_wait();
	mode_switch();
	pi_wait();
	lock_stealing();
	simple_condwait();
	recursive_condwait();
	fprintf(stderr, "Test OK\n");
	return 0;
}


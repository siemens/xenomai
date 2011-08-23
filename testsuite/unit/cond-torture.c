/*
 * Functional testing of the condvar implementation for native & posix skins.
 *
 * Copyright (C) Gilles Chanteperdrix  <gilles.chanteperdrix@xenomai.org>
 *
 * Released under the terms of GPLv2.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <pthread.h>
#include <native/timer.h>

#ifndef XENO_POSIX
#include <native/task.h>
#include <native/mutex.h>
#include <native/cond.h>
#endif /* __NATIVE_SKIN */

#include <asm-generic/xenomai/stack.h>

#define NS_PER_MS (1000000)
#ifdef XENO_POSIX
#define NS_PER_S (1000000000)

typedef	pthread_t thread_t;
typedef pthread_mutex_t mutex_t;
typedef pthread_cond_t cond_t;

unsigned long long timer_read(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	return (unsigned long long)ts.tv_sec * NS_PER_S + ts.tv_nsec;
}

int mutex_init(mutex_t *mutex, int type, int pi)
{
	pthread_mutexattr_t mattr;
	int err;

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, type);
#ifdef HAVE_PTHREAD_MUTEXATTR_SETPROTOCOL
	if (pi != 0)
		pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);

	err = pthread_mutex_init(mutex, &mattr);
#else
	if (pi != 0) {
		err = ENOSYS;
		goto out;
	}
	err = pthread_mutex_init(mutex, &mattr);

  out:
#endif
	pthread_mutexattr_destroy(&mattr);

	return -err;
}
#define mutex_lock(mutex) (-pthread_mutex_lock(mutex))
#define mutex_unlock(mutex) (-pthread_mutex_unlock(mutex))
#define mutex_destroy(mutex) (-pthread_mutex_destroy(mutex))

int cond_init(cond_t *cond, int absolute)
{
	pthread_condattr_t cattr;
	int err;

	pthread_condattr_init(&cattr);
	pthread_condattr_setclock(&cattr,
				  absolute ? CLOCK_REALTIME : CLOCK_MONOTONIC);
	err = pthread_cond_init(cond, &cattr);
	pthread_condattr_destroy(&cattr);

	return -err;
}
#define cond_signal(cond) (-pthread_cond_signal(cond))

int cond_wait(cond_t *cond, mutex_t *mutex, unsigned long long ns)
{
	struct timespec ts;

	if (ns == XN_INFINITE)
		return -pthread_cond_wait(cond, mutex);

	clock_gettime(CLOCK_MONOTONIC, &ts);
	ns += ts.tv_nsec;
	ts.tv_sec += ns / NS_PER_S;
	ts.tv_nsec = ns % NS_PER_S;

	return -pthread_cond_timedwait(cond, mutex, &ts);
}

int cond_wait_until(cond_t *cond, mutex_t *mutex, unsigned long long date)
{
	struct timespec ts = {
		.tv_sec = date / NS_PER_S,
		.tv_nsec = date % NS_PER_S,
	};

	return -pthread_cond_timedwait(cond, mutex, &ts);
}
#define cond_destroy(cond) (-pthread_cond_destroy(cond))

int thread_msleep(unsigned ms)
{
	struct timespec ts = {
		.tv_sec = (ms * NS_PER_MS) / NS_PER_S,
		.tv_nsec = (ms * NS_PER_MS) % NS_PER_S,
	};

	return -nanosleep(&ts, NULL);
}

int thread_spawn(thread_t *thread, int prio,
		 void *(*handler)(void *cookie), void *cookie)
{
	struct sched_param param;
	pthread_attr_t tattr;
	int err;

	pthread_attr_init(&tattr);
	pthread_attr_setinheritsched(&tattr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&tattr, SCHED_FIFO);
	param.sched_priority = prio;
	pthread_attr_setschedparam(&tattr, &param);
	pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_JOINABLE);
	pthread_attr_setstacksize(&tattr, xeno_stacksize(0));

	err = pthread_create(thread, &tattr, handler, cookie);

	pthread_attr_destroy(&tattr);

	return -err;
}
#define thread_yield() sched_yield()
#define thread_kill(thread, sig) (-__real_pthread_kill(thread, sig))
#define thread_self() pthread_self()
#define thread_join(thread) (-pthread_join(thread, NULL))

#else /* __NATIVE_SKIN__ */
typedef RT_MUTEX mutex_t;
typedef RT_TASK *thread_t;
typedef RT_COND cond_t;

#define timer_read() rt_timer_read()

int __mutex_init(mutex_t *mutex, const char *name, int type, int pi)
{
	if (type == PTHREAD_MUTEX_ERRORCHECK)
		return -EINVAL;
	(void)(pi);

	return -rt_mutex_create(mutex, name);
}
#define mutex_init(mutex, type, pi) __mutex_init(mutex, #mutex, type, pi)
#define mutex_destroy(mutex) rt_mutex_delete(mutex)
#define mutex_lock(mutex) rt_mutex_acquire(mutex, TM_INFINITE)
#define mutex_unlock(mutex) rt_mutex_release(mutex)

int __cond_init(cond_t *cond, const char *name, int absolute)
{
	(void)(absolute);
	return rt_cond_create(cond, name);
}
#define cond_init(cond, absolute) __cond_init(cond, #cond, absolute)
#define cond_signal(cond) rt_cond_signal(cond)
#define cond_wait(cond, mutex, ns) rt_cond_wait(cond, mutex, (RTIME)ns)
#define cond_wait_until(cond, mutex, ns) \
	rt_cond_wait_until(cond, mutex, (RTIME)ns)
#define cond_destroy(cond) rt_cond_delete(cond)

#define thread_self() rt_task_self()
#define thread_msleep(ms) rt_task_sleep((RTIME)ms * NS_PER_MS)
int
thread_spawn_inner(thread_t *thread, const char *name,
		   int prio, void *(*handler)(void *), void *cookie)
{
	thread_t tcb;
	int err;

	tcb = malloc(sizeof(*tcb));
	if (!tcb)
		return -ENOSPC;

	err = rt_task_spawn(tcb, name, 0, prio, T_JOINABLE,
			    (void (*)(void *))handler, cookie);
	if (!err)
		*thread = tcb;

	return err;
}
#define thread_spawn(thread, prio, handler, cookie) \
	thread_spawn_inner(thread, #handler, prio, handler, cookie)
#define thread_yield() rt_task_yield()
#define thread_kill(thread, sig) \
	(-pthread_kill((pthread_t)thread->opaque2, sig))
#define thread_join(thread) rt_task_join(thread)

#endif /* __NATIVE_SKIN__ */

void check_inner(const char *file, int line, const char *fn, const char *msg, int status, int expected)
{
	if (status == expected)
		return;

	fprintf(stderr, "FAILED %s %s: returned %d instead of %d - %s\n",
		fn, msg, status, expected, strerror(-status));
	exit(EXIT_FAILURE);
}
#define check(msg, status, expected) \
	check_inner(__FILE__, __LINE__, __FUNCTION__, msg, status, expected)

#define check_unix(msg, status, expected)				\
	({								\
		int s = (status);					\
		check_inner(__FILE__, __LINE__, __FUNCTION__, msg, s < 0 ? -errno : s, expected); \
	})

void check_sleep_inner(const char *fn,
		       const char *prefix, unsigned long long start)
{
	unsigned long long diff = rt_timer_tsc2ns(rt_timer_tsc() - start);

	if (diff < 10 * NS_PER_MS) {
		fprintf(stderr, "%s waited %Ld.%03u us\n",
			prefix, diff / 1000, (unsigned)(diff % 1000));
		exit(EXIT_FAILURE);
	}
}
#define check_sleep(prefix, start) \
	check_sleep_inner(__FUNCTION__, prefix, start)

struct cond_mutex {
	mutex_t *mutex;
	cond_t *cond;
	thread_t tid;
};

void *cond_signaler(void *cookie)
{
	unsigned long long start;
	struct cond_mutex *cm = cookie;

	start = rt_timer_tsc();
	check("mutex_lock", mutex_lock(cm->mutex), 0);
	check_sleep("mutex_lock", start);
	thread_msleep(10);
	check("cond_signal", cond_signal(cm->cond), 0);
	check("mutex_unlock", mutex_unlock(cm->mutex), 0);

	return NULL;
}

void simple_condwait(void)
{
	unsigned long long start;
	mutex_t mutex;
	cond_t cond;
	struct cond_mutex cm = {
		.mutex = &mutex,
		.cond = &cond,
	};
	thread_t cond_signaler_tid;

	fprintf(stderr, "%s\n", __FUNCTION__);

	check("mutex_init", mutex_init(&mutex, PTHREAD_MUTEX_DEFAULT, 0), 0);
	check("cond_init", cond_init(&cond, 0), 0);
	check("mutex_lock", mutex_lock(&mutex), 0);
	check("thread_spawn",
	      thread_spawn(&cond_signaler_tid, 2, cond_signaler, &cm), 0);
	thread_msleep(11);

	start = rt_timer_tsc();
	check("cond_wait", cond_wait(&cond, &mutex, XN_INFINITE), 0);
	check_sleep("cond_wait", start);
	thread_msleep(10);
	check("mutex_unlock", mutex_unlock(&mutex), 0);
	check("thread_join", thread_join(cond_signaler_tid), 0);
	check("mutex_destroy", mutex_destroy(&mutex), 0);
	check("cond_destroy", cond_destroy(&cond), 0);
}

void relative_condwait(void)
{
	unsigned long long start;
	mutex_t mutex;
	cond_t cond;

	fprintf(stderr, "%s\n", __FUNCTION__);

	check("mutex_init", mutex_init(&mutex, PTHREAD_MUTEX_DEFAULT, 0), 0);
	check("cond_init", cond_init(&cond, 0), 0);
	check("mutex_lock", mutex_lock(&mutex), 0);

	start = rt_timer_tsc();
	check("cond_wait",
	      cond_wait(&cond, &mutex, 10 * NS_PER_MS), -ETIMEDOUT);
	check_sleep("cond_wait", start);
	thread_msleep(10);

	check("mutex_unlock", mutex_unlock(&mutex), 0);
	check("mutex_destroy", mutex_destroy(&mutex), 0);
	check("cond_destroy", cond_destroy(&cond), 0);
}

void absolute_condwait(void)
{
	unsigned long long start;
	mutex_t mutex;
	cond_t cond;

	fprintf(stderr, "%s\n", __FUNCTION__);

	check("mutex_init", mutex_init(&mutex, PTHREAD_MUTEX_DEFAULT, 0), 0);
	check("cond_init", cond_init(&cond, 1), 0);
	check("mutex_lock", mutex_lock(&mutex), 0);

	start = rt_timer_tsc();
	check("cond_wait",
	      cond_wait_until(&cond, &mutex, timer_read() + 10 * NS_PER_MS),
	      -ETIMEDOUT);
	check_sleep("cond_wait", start);

	check("mutex_unlock", mutex_unlock(&mutex), 0);
	check("mutex_destroy", mutex_destroy(&mutex), 0);
	check("cond_destroy", cond_destroy(&cond), 0);
}

void *cond_killer(void *cookie)
{
	unsigned long long start;
	struct cond_mutex *cm = cookie;

	start = rt_timer_tsc();
	check("mutex_lock", mutex_lock(cm->mutex), 0);
	check_sleep("mutex_lock", start);
	thread_msleep(10);
	check("thread_kill", thread_kill(cm->tid, SIGRTMIN), 0);
	check("mutex_unlock", mutex_unlock(cm->mutex), 0);

	return NULL;
}

volatile int sig_seen;

void sighandler(int sig)
{
	++sig_seen;
}

void sig_norestart_condwait(void)
{
	unsigned long long start;
	mutex_t mutex;
	cond_t cond;
	struct cond_mutex cm = {
		.mutex = &mutex,
		.cond = &cond,
		.tid = thread_self(),
	};
	thread_t cond_killer_tid;
	struct sigaction sa = {
		.sa_handler = sighandler,
		.sa_flags = 0,
	};
	sigemptyset(&sa.sa_mask);

	fprintf(stderr, "%s\n", __FUNCTION__);

	check_unix("sigaction", sigaction(SIGRTMIN, &sa, NULL), 0);
	check("mutex_init", mutex_init(&mutex, PTHREAD_MUTEX_DEFAULT, 0), 0);
	check("cond_init", cond_init(&cond, 0), 0);
	check("mutex_lock", mutex_lock(&mutex), 0);
	check("thread_spawn",
	      thread_spawn(&cond_killer_tid, 2, cond_killer, &cm), 0);
	thread_msleep(11);

	start = rt_timer_tsc();
	sig_seen = 0;
#ifdef XENO_POSIX
	check("cond_wait", cond_wait(&cond, &mutex, XN_INFINITE), 0);
#else /* native */
	{
		int err = cond_wait(&cond, &mutex, XN_INFINITE);
		if (err == 0)
			err = -EINTR;
		check("cond_wait", err, -EINTR);
	}
#endif /* native */
	check_sleep("cond_wait", start);
	check("sig_seen", sig_seen, 1);
	check("mutex_unlock", mutex_unlock(&mutex), 0);
	check("thread_join", thread_join(cond_killer_tid), 0);
	check("mutex_destroy", mutex_destroy(&mutex), 0);
	check("cond_destroy", cond_destroy(&cond), 0);
}

void sig_restart_condwait(void)
{
	unsigned long long start;
	mutex_t mutex;
	cond_t cond;
	struct cond_mutex cm = {
		.mutex = &mutex,
		.cond = &cond,
		.tid = thread_self(),
	};
	thread_t cond_killer_tid;
	struct sigaction sa = {
		.sa_handler = sighandler,
		.sa_flags = 0,
	};
	sigemptyset(&sa.sa_mask);

	fprintf(stderr, "%s\n", __FUNCTION__);

	check_unix("sigaction", sigaction(SIGRTMIN, &sa, NULL), 0);
	check("mutex_init", mutex_init(&mutex, PTHREAD_MUTEX_DEFAULT, 0), 0);
	check("cond_init", cond_init(&cond, 0), 0);
	check("mutex_lock", mutex_lock(&mutex), 0);
	check("thread_spawn",
	      thread_spawn(&cond_killer_tid, 2, cond_killer, &cm), 0);
	thread_msleep(11);

	start = rt_timer_tsc();
	sig_seen = 0;
#ifdef XENO_POSIX
	check("cond_wait", cond_wait(&cond, &mutex, XN_INFINITE), 0);
#else /* native */
	{
		int err = cond_wait(&cond, &mutex, XN_INFINITE);
		if (err == 0)
			err = -EINTR;
		check("cond_wait", err, -EINTR);
	}
#endif /* native */
	check_sleep("cond_wait", start);
	check("sig_seen", sig_seen, 1);
	check("mutex_unlock", mutex_unlock(&mutex), 0);
	check("thread_join", thread_join(cond_killer_tid), 0);
	check("mutex_destroy", mutex_destroy(&mutex), 0);
	check("cond_destroy", cond_destroy(&cond), 0);
}

void *mutex_killer(void *cookie)
{
	unsigned long long start;
	struct cond_mutex *cm = cookie;

	start = rt_timer_tsc();
	check("mutex_lock", mutex_lock(cm->mutex), 0);
	check_sleep("mutex_lock", start);
	check("cond_signal", cond_signal(cm->cond), 0);
	thread_msleep(10);
	check("thread_kill", thread_kill(cm->tid, SIGRTMIN), 0);
	check("mutex_unlock", mutex_unlock(cm->mutex), 0);

	return NULL;
}

void sig_norestart_condwait_mutex(void)
{
	unsigned long long start;
	mutex_t mutex;
	cond_t cond;
	struct cond_mutex cm = {
		.mutex = &mutex,
		.cond = &cond,
		.tid = thread_self(),
	};
	thread_t mutex_killer_tid;
	struct sigaction sa = {
		.sa_handler = sighandler,
		.sa_flags = 0,
	};
	sigemptyset(&sa.sa_mask);

	fprintf(stderr, "%s\n", __FUNCTION__);

	check_unix("sigaction", sigaction(SIGRTMIN, &sa, NULL), 0);
	check("mutex_init", mutex_init(&mutex, PTHREAD_MUTEX_DEFAULT, 0), 0);
	check("cond_init", cond_init(&cond, 0), 0);
	check("mutex_lock", mutex_lock(&mutex), 0);
	check("thread_spawn",
	      thread_spawn(&mutex_killer_tid, 2, mutex_killer, &cm), 0);
	thread_msleep(11);

	sig_seen = 0;
	start = rt_timer_tsc();
	check("cond_wait", cond_wait(&cond, &mutex, XN_INFINITE), 0);
	check_sleep("cond_wait", start);
	check("sig_seen", sig_seen, 1);
	thread_msleep(10);

	check("mutex_unlock", mutex_unlock(&mutex), 0);
	check("thread_join", thread_join(mutex_killer_tid), 0);
	check("mutex_destroy", mutex_destroy(&mutex), 0);
	check("cond_destroy", cond_destroy(&cond), 0);
}

void sig_restart_condwait_mutex(void)
{
	unsigned long long start;
	mutex_t mutex;
	cond_t cond;
	struct cond_mutex cm = {
		.mutex = &mutex,
		.cond = &cond,
		.tid = thread_self(),
	};
	thread_t mutex_killer_tid;
	struct sigaction sa = {
		.sa_handler = sighandler,
		.sa_flags = SA_RESTART,
	};
	sigemptyset(&sa.sa_mask);

	fprintf(stderr, "%s\n", __FUNCTION__);

	check_unix("sigaction", sigaction(SIGRTMIN, &sa, NULL), 0);
	check("mutex_init", mutex_init(&mutex, PTHREAD_MUTEX_DEFAULT, 0), 0);
	check("cond_init", cond_init(&cond, 0), 0);
	check("mutex_lock", mutex_lock(&mutex), 0);
	check("thread_spawn",
	      thread_spawn(&mutex_killer_tid, 2, mutex_killer, &cm), 0);
	thread_msleep(11);

	sig_seen = 0;
	start = rt_timer_tsc();

	check("cond_wait", cond_wait(&cond, &mutex, XN_INFINITE), 0);
	check_sleep("cond_wait", start);
	thread_msleep(10);

	check("mutex_unlock", mutex_unlock(&mutex), 0);
	check("thread_join", thread_join(mutex_killer_tid), 0);
	check("mutex_destroy", mutex_destroy(&mutex), 0);
	check("cond_destroy", cond_destroy(&cond), 0);
}

void *double_killer(void *cookie)
{
	unsigned long long start;
	struct cond_mutex *cm = cookie;

	start = rt_timer_tsc();
	check("mutex_lock", mutex_lock(cm->mutex), 0);
	check_sleep("mutex_lock", start);
	check("thread_kill 1", thread_kill(cm->tid, SIGRTMIN), 0);
	thread_msleep(10);
	check("thread_kill 2", thread_kill(cm->tid, SIGRTMIN), 0);
	check("mutex_unlock", mutex_unlock(cm->mutex), 0);

	return NULL;
}

void sig_norestart_double(void)
{
	unsigned long long start;
	mutex_t mutex;
	cond_t cond;
	struct cond_mutex cm = {
		.mutex = &mutex,
		.cond = &cond,
		.tid = thread_self(),
	};
	thread_t double_killer_tid;
	struct sigaction sa = {
		.sa_handler = sighandler,
		.sa_flags = 0,
	};
	sigemptyset(&sa.sa_mask);

	fprintf(stderr, "%s\n", __FUNCTION__);

	check_unix("sigaction", sigaction(SIGRTMIN, &sa, NULL), 0);
	check("mutex_init", mutex_init(&mutex, PTHREAD_MUTEX_DEFAULT, 0), 0);
	check("cond_init", cond_init(&cond, 0), 0);
	check("mutex_lock", mutex_lock(&mutex), 0);
	check("thread_spawn",
	      thread_spawn(&double_killer_tid, 2, double_killer, &cm), 0);
	thread_msleep(11);

	sig_seen = 0;
	start = rt_timer_tsc();
	check("cond_wait", cond_wait(&cond, &mutex, XN_INFINITE), 0);
	check_sleep("cond_wait", start);
	check("sig_seen", sig_seen, 2);
	thread_msleep(10);

	check("mutex_unlock", mutex_unlock(&mutex), 0);
	check("thread_join", thread_join(double_killer_tid), 0);
	check("mutex_destroy", mutex_destroy(&mutex), 0);
	check("cond_destroy", cond_destroy(&cond), 0);
}

void sig_restart_double(void)
{
	unsigned long long start;
	mutex_t mutex;
	cond_t cond;
	struct cond_mutex cm = {
		.mutex = &mutex,
		.cond = &cond,
		.tid = thread_self(),
	};
	thread_t double_killer_tid;
	struct sigaction sa = {
		.sa_handler = sighandler,
		.sa_flags = SA_RESTART,
	};
	sigemptyset(&sa.sa_mask);

	fprintf(stderr, "%s\n", __FUNCTION__);

	check_unix("sigaction", sigaction(SIGRTMIN, &sa, NULL), 0);
	check("mutex_init", mutex_init(&mutex, PTHREAD_MUTEX_DEFAULT, 0), 0);
	check("cond_init", cond_init(&cond, 0), 0);
	check("mutex_lock", mutex_lock(&mutex), 0);
	check("thread_spawn",
	      thread_spawn(&double_killer_tid, 2, double_killer, &cm), 0);
	thread_msleep(11);

	sig_seen = 0;
	start = rt_timer_tsc();

	check("cond_wait", cond_wait(&cond, &mutex, XN_INFINITE), 0);
	check_sleep("cond_wait", start);
	check("sig_seen", sig_seen, 2);
	thread_msleep(10);

	check("mutex_unlock", mutex_unlock(&mutex), 0);
	check("thread_join", thread_join(double_killer_tid), 0);
	check("mutex_destroy", mutex_destroy(&mutex), 0);
	check("cond_destroy", cond_destroy(&cond), 0);
}

void *cond_destroyer(void *cookie)
{
	unsigned long long start;
	struct cond_mutex *cm = cookie;

	start = rt_timer_tsc();
	check("mutex_lock", mutex_lock(cm->mutex), 0);
	check_sleep("mutex_lock", start);
	thread_msleep(10);
#ifdef XENO_POSIX
	check("cond_destroy", cond_destroy(cm->cond), -EBUSY);
#else /* native */
	check("cond_destroy", cond_destroy(cm->cond), 0);
#endif /* native */
	check("mutex_unlock", mutex_unlock(cm->mutex), 0);

	return NULL;
}

void cond_destroy_whilewait(void)
{
	unsigned long long start;
	mutex_t mutex;
	cond_t cond;
	struct cond_mutex cm = {
		.mutex = &mutex,
		.cond = &cond,
		.tid = thread_self(),
	};
	thread_t cond_destroyer_tid;
	struct sigaction sa = {
		.sa_handler = sighandler,
		.sa_flags = SA_RESTART,
	};
	sigemptyset(&sa.sa_mask);

	fprintf(stderr, "%s\n", __FUNCTION__);

	check_unix("sigaction", sigaction(SIGRTMIN, &sa, NULL), 0);
	check("mutex_init", mutex_init(&mutex, PTHREAD_MUTEX_DEFAULT, 0), 0);
	check("cond_init", cond_init(&cond, 0), 0);
	check("mutex_lock", mutex_lock(&mutex), 0);
	check("thread_spawn",
	      thread_spawn(&cond_destroyer_tid, 2, cond_destroyer, &cm), 0);
	thread_msleep(11);

	start = rt_timer_tsc();

#ifdef XENO_POSIX
	check("cond_wait", cond_wait(&cond, &mutex, 10 * NS_PER_MS), -ETIMEDOUT);
	check_sleep("cond_wait", start);
	thread_msleep(10);

	check("mutex_unlock", mutex_unlock(&mutex), 0);
#else /* native */
	check("cond_wait", cond_wait(&cond, &mutex, XN_INFINITE), -EIDRM);
	check_sleep("cond_wait", start);
#endif /* native */
	check("thread_join", thread_join(cond_destroyer_tid), 0);
	check("mutex_destroy", mutex_destroy(&mutex), 0);
#ifdef XENO_POSIX
	check("cond_destroy", cond_destroy(&cond), 0);
#else /* native */
	check("cond_destroy", cond_destroy(&cond), -ESRCH);
#endif /* native */
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
	pthread_setschedparam(pthread_self(), SCHED_FIFO, &sparam);
#else /* __NATIVE_SKIN__ */
	rt_task_shadow(&main_tid, "main_task", 2, 0);
#endif /* __NATIVE_SKIN__ */

	simple_condwait();
	relative_condwait();
	absolute_condwait();
	sig_norestart_condwait();
	sig_restart_condwait();
	sig_norestart_condwait_mutex();
	sig_restart_condwait_mutex();
	sig_norestart_double();
	sig_restart_double();
	cond_destroy_whilewait();
	fprintf(stderr, "Test OK\n");

	return 0;
}

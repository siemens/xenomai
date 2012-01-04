/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <semaphore.h>
#include <cobalt/syscall.h>
#include <asm-generic/bits/current.h>
#include <asm-generic/stack.h>
#include "internal.h"

static pthread_attr_ex_t default_attr_ex;

static int linuxthreads;

int __wrap_pthread_setschedparam(pthread_t thread,
				 int policy, const struct sched_param *param)
{
	volatile pthread_t myself = pthread_self();
	unsigned long mode_offset;
	int ret, promoted;

	if (thread == myself)
		xeno_fault_stack();

	ret = -XENOMAI_SKINCALL5(__cobalt_muxid,
				 sc_cobalt_thread_setschedparam,
				 thread, policy, param,
				 &mode_offset, &promoted);

	if (ret == EPERM)
		return __STD(pthread_setschedparam(thread, policy, param));

	if (ret == 0 && promoted) {
		xeno_sigshadow_install_once();
		xeno_set_current();
		xeno_set_current_window(mode_offset);
		if (policy != SCHED_OTHER)
			XENOMAI_SYSCALL1(sc_nucleus_migrate, XENOMAI_XENO_DOMAIN);
	}

	return ret;
}

int pthread_setschedparam_ex(pthread_t thread,
			     int policy, const struct sched_param_ex *param)
{
	volatile pthread_t myself = pthread_self();
	struct sched_param short_param;
	unsigned long mode_offset;
	int ret, promoted;

	if (thread == myself)
		xeno_fault_stack();

	ret = -XENOMAI_SKINCALL5(__cobalt_muxid,
				 sc_cobalt_thread_setschedparam_ex,
				 thread, policy, param,
				 &mode_offset, &promoted);
	if (ret == EPERM) {
		short_param.sched_priority = param->sched_priority;
		return __STD(pthread_setschedparam(thread, policy, &short_param));
	}

	if (ret == 0 && promoted) {
		xeno_sigshadow_install_once();
		xeno_set_current();
		xeno_set_current_window(mode_offset);
		if (policy != SCHED_OTHER)
			XENOMAI_SYSCALL1(sc_nucleus_migrate, XENOMAI_XENO_DOMAIN);
	}

	return ret;
}

int __wrap_pthread_getschedparam(pthread_t thread,
				 int *__restrict__ policy,
				 struct sched_param *__restrict__ param)
{
	int ret;

	ret = -XENOMAI_SKINCALL3(__cobalt_muxid,
				 sc_cobalt_thread_getschedparam,
				 thread, policy, param);
	if (ret == ESRCH)
		return __STD(pthread_getschedparam(thread, policy, param));

	return ret;
}

int pthread_getschedparam_ex(pthread_t thread,
			     int *__restrict__ policy,
			     struct sched_param_ex *__restrict__ param)
{
	struct sched_param short_param;
	int ret;

	ret = -XENOMAI_SKINCALL3(__cobalt_muxid,
				 sc_cobalt_thread_getschedparam_ex,
				 thread, policy, param);
	if (ret == ESRCH) {
		ret = __STD(pthread_getschedparam(thread, policy, &short_param));
		if (ret == 0)
			param->sched_priority = short_param.sched_priority;
	}

	return ret;
}

int __wrap_sched_yield(void)
{
	int ret;

	ret = -XENOMAI_SKINCALL0(__cobalt_muxid, sc_cobalt_sched_yield);
	if (ret == -1)
		ret = __STD(sched_yield());

	return ret;
}

int __wrap_sched_get_priority_min(int policy)
{
	int ret;

	ret = XENOMAI_SKINCALL1(__cobalt_muxid, sc_cobalt_sched_minprio, policy);
	if (ret < 0) {
		if (ret == -ENOSYS)
			return __STD(sched_get_priority_min(policy));
		errno = -ret;
		ret = -1;
	}

	return ret;
}

int __wrap_sched_get_priority_max(int policy)
{
	int ret;

	ret = XENOMAI_SKINCALL1(__cobalt_muxid, sc_cobalt_sched_maxprio, policy);
	if (ret < 0) {
		if (ret == -ENOSYS)
			return __STD(sched_get_priority_max(policy));
		errno = -ret;
		ret = -1;
	}

	return ret;
}

int __wrap_pthread_yield(void)
{
	return __wrap_sched_yield();
}

struct pthread_iargs {
	struct sched_param_ex param_ex;
	int policy;
	void *(*start)(void *);
	void *arg;
	int parent_prio;
	sem_t sync;
	int ret;
};

static void *__pthread_trampoline(void *p)
{
	/*
	 * Volatile is to prevent (too) smart gcc releases from
	 * trashing the syscall registers (see later comment).
	 */
	volatile pthread_t tid = pthread_self();
	struct pthread_iargs *iargs = p;
	struct sched_param_ex param_ex;
	void *(*start)(void *), *arg;
	unsigned long mode_offset;
	int parent_prio, policy;
	long ret;

	xeno_sigshadow_install_once();

	param_ex = iargs->param_ex;
	policy = iargs->policy;
	parent_prio = iargs->parent_prio;
	start = iargs->start;
	arg = iargs->arg;

	/*
	 * Do _not_ inline the call to pthread_self() in the syscall
	 * macro: this trashes the syscall regs on some archs.
	 */
	ret = XENOMAI_SKINCALL4(__cobalt_muxid, sc_cobalt_thread_create, tid,
				policy, &param_ex, &mode_offset);
	if (ret == 0) {
		xeno_set_current();
		xeno_set_current_window(mode_offset);
	}

	/*
	 * We must access anything we'll need from *iargs before
	 * posting the sync semaphore, since our released parent could
	 * unwind the stack space onto which the iargs struct is laid
	 * on before we actually get the CPU back.
	 */
	iargs->ret = -ret;
	__STD(sem_post(&iargs->sync));
	if (ret)
		return (void *)-ret;

	/*
	 * If the parent thread runs with the same priority as we do,
	 * then we should yield the CPU to it, to preserve the
	 * scheduling order.
	 */
	if (param_ex.sched_priority == parent_prio)
		__wrap_sched_yield();

	if (policy != SCHED_OTHER)
		XENOMAI_SYSCALL1(sc_nucleus_migrate, XENOMAI_XENO_DOMAIN);

	return start(arg);
}

int pthread_create_ex(pthread_t *tid,
		      const pthread_attr_ex_t *attr_ex,
		      void *(*start) (void *), void *arg)
{
	struct pthread_iargs iargs;
	struct sched_param param;
	pthread_attr_t attr;
	int inherit, ret;
	pthread_t ltid;
	size_t stksz;

	if (attr_ex == NULL)
		attr_ex = &default_attr_ex;

	pthread_getschedparam_ex(pthread_self(), &iargs.policy, &iargs.param_ex);
	iargs.parent_prio = iargs.param_ex.sched_priority;
	memcpy(&attr, &attr_ex->std, sizeof(attr));

	pthread_attr_getinheritsched(&attr, &inherit);
	if (inherit == PTHREAD_EXPLICIT_SCHED) {
		pthread_attr_getschedpolicy_ex(attr_ex, &iargs.policy);
		pthread_attr_getschedparam_ex(attr_ex, &iargs.param_ex);
	}

	if (linuxthreads && geteuid()) {
		/*
		 * Work around linuxthreads shortcoming: it doesn't
		 * believe that it could have RT power as non-root and
		 * fails the thread creation overeagerly.
		 */
		pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
		param.sched_priority = 0;
		pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
		pthread_attr_setschedparam(&attr, &param);
	} else
		/*
		 * Get the created thread to temporarily inherit the
		 * caller priority (we mean linux/libc priority here,
		 * as we use a libc call to create the thread).
		 */
		pthread_attr_setinheritsched(&attr, PTHREAD_INHERIT_SCHED);

	pthread_attr_getstacksize(&attr, &stksz);
	pthread_attr_setstacksize(&attr, xeno_stacksize(stksz));

	/*
	 * First start a native POSIX thread, then mate a Xenomai
	 * shadow to it.
	 */
	iargs.start = start;
	iargs.arg = arg;
	iargs.ret = EAGAIN;
	__STD(sem_init(&iargs.sync, 0, 0));

	ret = __STD(pthread_create(&ltid, &attr, &__pthread_trampoline, &iargs));
	if (ret)
		goto fail;

	while (__STD(sem_wait(&iargs.sync)) && errno == EINTR)
		;

	ret = iargs.ret;
	if (ret == 0)
		*tid = ltid;
fail:
	__STD(sem_destroy(&iargs.sync));

	return ret;
}

int __wrap_pthread_create(pthread_t *tid,
			  const pthread_attr_t *attr,
			  void *(*start) (void *), void *arg)
{
	pthread_attr_ex_t attr_ex;
	struct sched_param param;
	int policy;

	if (attr == NULL)
		attr = &default_attr_ex.std;

	memcpy(&attr_ex.std, attr, sizeof(*attr));
	pthread_attr_getschedpolicy(attr, &policy);
	attr_ex.nonstd.sched_policy = policy;
	pthread_attr_getschedparam(attr, &param);
	attr_ex.nonstd.sched_param.sched_priority = param.sched_priority;

	return pthread_create_ex(tid, &attr_ex, start, arg);
}

int pthread_make_periodic_np(pthread_t thread,
			     clockid_t clk_id,
			     struct timespec *starttp,
			     struct timespec *periodtp)
{
	return -XENOMAI_SKINCALL4(__cobalt_muxid,
				  sc_cobalt_thread_make_periodic,
				  thread, clk_id, starttp, periodtp);
}

int pthread_wait_np(unsigned long *overruns_r)
{
	int ret, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	ret = -XENOMAI_SKINCALL1(__cobalt_muxid,
				 sc_cobalt_thread_wait, overruns_r);

	pthread_setcanceltype(oldtype, NULL);

	return ret;
}

int pthread_set_mode_np(int clrmask, int setmask, int *mode_r)
{
	return -XENOMAI_SKINCALL3(__cobalt_muxid,
				  sc_cobalt_thread_set_mode,
				  clrmask, setmask, mode_r);
}

int pthread_set_name_np(pthread_t thread, const char *name)
{
	return -XENOMAI_SKINCALL2(__cobalt_muxid,
				  sc_cobalt_thread_set_name, thread, name);
}

int pthread_probe_np(pid_t tid)
{
	return XENOMAI_SKINCALL1(__cobalt_muxid,
				 sc_cobalt_thread_probe, tid);
}

int __wrap_pthread_kill(pthread_t thread, int sig)
{
	int ret;

	ret = -XENOMAI_SKINCALL2(__cobalt_muxid,
				 sc_cobalt_thread_kill, thread, sig);
	if (ret == ESRCH)
		return __STD(pthread_kill(thread, sig));

	return ret;
}

static __attribute__((constructor)) void cobalt_thread_init(void)
{
	pthread_attr_init_ex(&default_attr_ex);
#ifdef _CS_GNU_LIBPTHREAD_VERSION
	{
		char vers[128];
		linuxthreads =
			!confstr(_CS_GNU_LIBPTHREAD_VERSION, vers, sizeof(vers))
			|| strstr(vers, "linuxthreads");
	}
#else /* !_CS_GNU_LIBPTHREAD_VERSION */
	linuxthreads = 1;
#endif /* !_CS_GNU_LIBPTHREAD_VERSION */
}

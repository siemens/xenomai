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
#include <posix/syscall.h>
#include <asm-generic/bits/current.h>
#include <asm-generic/bits/sigshadow.h>
#include <asm-generic/stack.h>

extern int __pse51_muxid;

static pthread_attr_t default_attr;
static int linuxthreads;

int __wrap_pthread_setschedparam(pthread_t thread,
				 int policy, const struct sched_param *param)
{
	pthread_t myself = pthread_self();
	unsigned long mode_offset;
	int err, promoted;

	if (thread == myself)
		xeno_fault_stack();

	err = -XENOMAI_SKINCALL5(__pse51_muxid,
				 __pse51_thread_setschedparam,
				 thread, policy, param,
				 &mode_offset, &promoted);

	if (err == EPERM)
		return __real_pthread_setschedparam(thread, policy, param);

	if (!err && promoted) {
		xeno_sigshadow_install_once();
		xeno_set_current();
		xeno_set_current_mode(mode_offset);
		if (policy != SCHED_OTHER)
			XENOMAI_SYSCALL1(__xn_sys_migrate, XENOMAI_XENO_DOMAIN);
	}

	return err;
}

int pthread_setschedparam_ex(pthread_t thread,
			     int policy, const struct sched_param_ex *param)
{
	pthread_t myself = pthread_self();
	struct sched_param short_param;
	unsigned long mode_offset;
	int err, promoted;

	if (thread == myself)
		xeno_fault_stack();

	err = -XENOMAI_SKINCALL5(__pse51_muxid,
				 __pse51_thread_setschedparam_ex,
				 thread, policy, param,
				 &mode_offset, &promoted);

	if (err == EPERM) {
		short_param.sched_priority = param->sched_priority;
		return __real_pthread_setschedparam(thread, policy, &short_param);
	}

	if (!err && promoted) {
		xeno_sigshadow_install_once();
		xeno_set_current();
		xeno_set_current_mode(mode_offset);
		if (policy != SCHED_OTHER)
			XENOMAI_SYSCALL1(__xn_sys_migrate, XENOMAI_XENO_DOMAIN);
	}

	return err;
}

int __wrap_pthread_getschedparam(pthread_t thread,
				 int *__restrict__ policy,
				 struct sched_param *__restrict__ param)
{
	int err;

	err = -XENOMAI_SKINCALL3(__pse51_muxid,
				 __pse51_thread_getschedparam,
				 thread, policy, param);

	if (err == ESRCH)
		return __real_pthread_getschedparam(thread, policy, param);

	return err;
}

int pthread_getschedparam_ex(pthread_t thread,
			     int *__restrict__ policy,
			     struct sched_param_ex *__restrict__ param)
{
	struct sched_param short_param;
	int err;

	err = -XENOMAI_SKINCALL3(__pse51_muxid,
				 __pse51_thread_getschedparam_ex,
				 thread, policy, param);

	if (err == ESRCH) {
		err = __real_pthread_getschedparam(thread, policy, &short_param);
		if (err == 0)
			param->sched_priority = short_param.sched_priority;
	}

	return err;
}

int __wrap_sched_yield(void)
{
	int err = -XENOMAI_SKINCALL0(__pse51_muxid, __pse51_sched_yield);

	if (err == -1)
		err = __real_sched_yield();

	return err;
}

int __wrap_pthread_yield(void)
{
	return __wrap_sched_yield();
}

struct pthread_iargs {
	void *(*start) (void *);
	void *arg;
	int policy;
	int parent_prio, prio;
	sem_t sync;
	int ret;
};

static void *__pthread_trampoline(void *arg)
{
	struct pthread_iargs *iargs = (struct pthread_iargs *)arg;
	/* Avoid smart versions of gcc to trashes the syscall
	   registers (again, see later comment). */
	volatile pthread_t tid = pthread_self();
	void *(*start) (void *), *cookie;
	unsigned long mode_offset;
	struct sched_param param;
	void *status = NULL;
	int parent_prio, policy;
	long err;

	xeno_sigshadow_install_once();

	param.sched_priority = iargs->prio;
	policy = iargs->policy;
	parent_prio = iargs->parent_prio;

	__real_pthread_setschedparam(pthread_self(), policy, &param);

	/* Do _not_ inline the call to pthread_self() in the syscall
	   macro: this trashes the syscall regs on some archs. */
	err = XENOMAI_SKINCALL4(__pse51_muxid, __pse51_thread_create, tid,
				iargs->policy, iargs->prio, &mode_offset);
	iargs->ret = -err;

	/* We must save anything we'll need to use from *iargs on our own
	   stack now before posting the sync sema4, since our released
	   parent could unwind the stack space onto which the iargs struct
	   is laid on before we actually get the CPU back. */

	start = iargs->start;
	cookie = iargs->arg;

	if (!err) {
		xeno_set_current();
		xeno_set_current_mode(mode_offset);
	}

	__real_sem_post(&iargs->sync);

	if (!err) {
		/* If the thread running pthread_create runs with the same
		   priority as us, we should leave it running, as if there never
		   was a synchronization with a semaphore. */
		if (param.sched_priority == parent_prio)
			__wrap_sched_yield();

		if (policy != SCHED_OTHER)
			XENOMAI_SYSCALL1(__xn_sys_migrate, XENOMAI_XENO_DOMAIN);
		status = start(cookie);
	} else
		status = (void *)-err;

	return status;
}

int __wrap_pthread_create(pthread_t *tid,
			  const pthread_attr_t * attr,
			  void *(*start) (void *), void *arg)
{
	struct pthread_iargs iargs;
	struct sched_param param;
	pthread_attr_t iattr;
	int inherit, err;
	pthread_t ltid;
	size_t stksz;

	if (!attr)
		attr = &default_attr;

	pthread_attr_getinheritsched(attr, &inherit);
	__wrap_pthread_getschedparam(pthread_self(), &iargs.policy, &param);
	iargs.parent_prio = param.sched_priority;
	if (inherit == PTHREAD_EXPLICIT_SCHED) {
		pthread_attr_getschedpolicy(attr, &iargs.policy);
		pthread_attr_getschedparam(attr, &param);
	}
	iargs.prio = param.sched_priority;

	memcpy(&iattr, attr, sizeof(pthread_attr_t));
	if (linuxthreads && geteuid()) {
		/* Work around linuxthreads shortcoming: it doesn't believe
		   that it could have RT power as non-root and fails the
		   thread creation overeagerly. */
		pthread_attr_setinheritsched(&iattr, PTHREAD_EXPLICIT_SCHED);
		param.sched_priority = 0;
		pthread_attr_setschedpolicy(&iattr, SCHED_OTHER);
		pthread_attr_setschedparam(&iattr, &param);
	} else
		/* Get the created thread to temporarily inherit pthread_create
		   caller priority */
		pthread_attr_setinheritsched(&iattr, PTHREAD_INHERIT_SCHED);
	pthread_attr_getstacksize(&iattr, &stksz);
	pthread_attr_setstacksize(&iattr, xeno_stacksize(stksz));
	attr = &iattr;

	/* First start a native POSIX thread, then associate a Xenomai shadow to
	   it. */

	iargs.start = start;
	iargs.arg = arg;
	iargs.ret = EAGAIN;
	__real_sem_init(&iargs.sync, 0, 0);

	err = __real_pthread_create(&ltid, attr,
				    &__pthread_trampoline, &iargs);

	if (!err)
		while (__real_sem_wait(&iargs.sync) && errno == EINTR) ;
	__real_sem_destroy(&iargs.sync);

	err = err ?: iargs.ret;

	if (!err)
		*tid = ltid;

	return err;
}

int pthread_make_periodic_np(pthread_t thread,
			     struct timespec *starttp,
			     struct timespec *periodtp)
{
	return -XENOMAI_SKINCALL3(__pse51_muxid,
				  __pse51_thread_make_periodic,
				  thread, starttp, periodtp);
}

int pthread_wait_np(unsigned long *overruns_r)
{
	int err, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	err = -XENOMAI_SKINCALL1(__pse51_muxid,
				 __pse51_thread_wait, overruns_r);

	pthread_setcanceltype(oldtype, NULL);

	return err;
}

int pthread_set_mode_np(int clrmask, int setmask)
{
	int err;

	err = -XENOMAI_SKINCALL2(__pse51_muxid,
				 __pse51_thread_set_mode, clrmask, setmask);

	return err;
}

int pthread_set_name_np(pthread_t thread, const char *name)
{
	return -XENOMAI_SKINCALL2(__pse51_muxid,
				  __pse51_thread_set_name, thread, name);
}

int sched_setconfig_np(int cpu, int policy,
		       union sched_config *config, size_t len)
{
	return -XENOMAI_SKINCALL4(__pse51_muxid,
				  __pse51_sched_setconfig_np,
				  cpu, policy, config, len);
}

int __wrap_pthread_kill(pthread_t thread, int sig)
{
	int err;
	err = -XENOMAI_SKINCALL2(__pse51_muxid,
				 __pse51_thread_kill, thread, sig);

	if (err == ESRCH)
		return __real_pthread_kill(thread, sig);

	return err;
}

static __attribute__((constructor)) void pse51_thread_init(void)
{
	pthread_attr_init(&default_attr);
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

/*
 * Copyright (C) 2011 Philippe Gerum <rpm@xenomai.org>.
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

#include <sys/types.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <boilerplate/ancillaries.h>
#include <copperplate/clockobj.h>
#include <copperplate/threadobj.h>
#include <copperplate/init.h>
#include "internal.h"

static int thread_spawn_prologue(struct corethread_attributes *cta);

static int thread_spawn_epilogue(struct corethread_attributes *cta);

static void *thread_trampoline(void *arg);

pid_t copperplate_get_tid(void)
{
	/*
	 * The nucleus maintains a hash table indexed on
	 * task_pid_vnr() values for mapped shadows. This is what
	 * __NR_gettid retrieves as well in Cobalt mode.
	 */
	return syscall(__NR_gettid);
}

#ifdef CONFIG_XENO_COBALT

#include "cobalt/internal.h"

int copperplate_create_thread(struct corethread_attributes *cta,
			      pthread_t *tid)
{
	pthread_attr_ex_t attr_ex;
	size_t stacksize;
	int ret;

	ret = thread_spawn_prologue(cta);
	if (ret)
		return __bt(ret);

	stacksize = cta->stacksize;
	if (stacksize < PTHREAD_STACK_MIN * 4)
		stacksize = PTHREAD_STACK_MIN * 4;

	pthread_attr_init_ex(&attr_ex);
	pthread_attr_setinheritsched_ex(&attr_ex, PTHREAD_INHERIT_SCHED);
	pthread_attr_setstacksize_ex(&attr_ex, stacksize);
	pthread_attr_setdetachstate_ex(&attr_ex, cta->detachstate);
	ret = __bt(-pthread_create_ex(tid, &attr_ex, thread_trampoline, cta));
	pthread_attr_destroy_ex(&attr_ex);
	if (ret)
		return __bt(ret);

	return __bt(thread_spawn_epilogue(cta));
}

int copperplate_renice_local_thread(pthread_t tid,
				    const struct coresched_attributes *csa)
{
	return __bt(-pthread_setschedparam_ex(tid, csa->policy, &csa->param));
}

static inline void prepare_wait_corespec(void)
{
	/*
	 * Switch back to primary mode eagerly, so that both the
	 * parent and the child threads compete on the same priority
	 * scale when handshaking. In addition, this ensures the child
	 * thread enters the run() handler over the Xenomai domain,
	 * which is a basic assumption for all clients.
	 */
	cobalt_thread_harden();
}

int copperplate_kill_tid(pid_t tid, int sig)
{
	return __RT(kill(tid, sig)) ? -errno : 0;
}

#else /* CONFIG_XENO_MERCURY */

int copperplate_kill_tid(pid_t tid, int sig)
{
	return syscall(__NR_tkill, tid, sig) ? -errno : 0;
}

int copperplate_create_thread(struct corethread_attributes *cta,
			      pthread_t *tid)
{
	pthread_attr_t attr;
	size_t stacksize;
	int ret;

	ret = thread_spawn_prologue(cta);
	if (ret)
		return __bt(ret);

	stacksize = cta->stacksize;
	if (stacksize < PTHREAD_STACK_MIN * 4)
		stacksize = PTHREAD_STACK_MIN * 4;

	pthread_attr_init(&attr);
	pthread_attr_setinheritsched(&attr, PTHREAD_INHERIT_SCHED);
	pthread_attr_setstacksize(&attr, stacksize);
	pthread_attr_setdetachstate(&attr, cta->detachstate);
	ret = __bt(-pthread_create(tid, &attr, thread_trampoline, cta));
	pthread_attr_destroy(&attr);

	if (ret)
		return __bt(ret);

	return __bt(thread_spawn_epilogue(cta));
}

int copperplate_renice_local_thread(pthread_t tid,
				    const struct coresched_attributes *csa)
{
	return __bt(-__RT(pthread_setschedparam(tid, csa->policy, &csa->param)));
}

static inline void prepare_wait_corespec(void)
{
	/* empty */
}

#endif  /* CONFIG_XENO_MERCURY */

static int thread_spawn_prologue(struct corethread_attributes *cta)
{
	int ret;

	ret = __RT(sem_init(&cta->__reserved.warm, 0, 0));
	if (ret)
		return __bt(-errno);

	cta->__reserved.status = -ENOSYS;

	return 0;
}

static void thread_spawn_wait(sem_t *sem)
{
	int ret;

	for (;;) {
		ret = __RT(sem_wait(sem));
		if (ret && errno == EINTR)
			continue;
		if (ret == 0)
			return;
		ret = -errno;
		panic("sem_wait() failed with %s", symerror(ret));
	}
}

static void *thread_trampoline(void *arg)
{
	struct corethread_attributes *cta = arg, _cta;
	sem_t released;
	int ret;

	/*
	 * cta may be on the parent's stack, so it may be dandling
	 * soon after the parent is posted: copy this argument
	 * structure early on.
	 */
	_cta = *cta;
	ret = cta->prologue(cta->arg);
	cta->__reserved.status = ret;
	if (ret)
		goto fail;

	ret = __bt(-__RT(sem_init(&released, 0, 0)));
	if (ret) {
		ret = -errno;
		cta->__reserved.status = ret;
		goto fail;
	}

	cta->__reserved.released = &released;
	/*
	 * CAUTION: over Cobalt, we have to switch back to primary
	 * mode _before_ releasing the parent thread, so that proper
	 * priority rules apply between the parent and child threads.
	 */
	prepare_wait_corespec();
	__RT(sem_post(&cta->__reserved.warm));
	thread_spawn_wait(&released);
	__RT(sem_destroy(&released));
	ret = __bt(copperplate_renice_local_thread(pthread_self(), &_cta.sched));
	if (ret)
		warning("cannot renice core thread, %s", symerror(ret));

	return _cta.run(_cta.arg);
fail:
	backtrace_check();
	__RT(sem_post(&cta->__reserved.warm));

	return (void *)(long)ret;
}

static int thread_spawn_epilogue(struct corethread_attributes *cta)
{
	prepare_wait_corespec();
	thread_spawn_wait(&cta->__reserved.warm);

	if (cta->__reserved.status == 0)
		__RT(sem_post(cta->__reserved.released));

	__RT(sem_destroy(&cta->__reserved.warm));

	return __bt(cta->__reserved.status);
}

void panic(const char *fmt, ...)
{
	struct threadobj *thobj = threadobj_current();
	va_list ap;

	va_start(ap, fmt);
	__panic(thobj ? threadobj_get_name(thobj) : NULL, fmt, ap);
}

void warning(const char *fmt, ...)
{
	struct threadobj *thobj = threadobj_current();
	va_list ap;

	va_start(ap, fmt);
	__warning(thobj ? threadobj_get_name(thobj) : NULL, fmt, ap);
	va_end(ap);
}

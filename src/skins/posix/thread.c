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

#include <errno.h>
#include <signal.h>
#include <xenomai/posix/syscall.h>
#include "posix/pthread.h"
#include "posix/semaphore.h"

extern int __pse51_muxid;

struct pthread_iargs {
    void *(*start)(void *);
    void *arg;
    int prio;
    sem_t sync;
    int ret;
};

static void __pthread_sigharden_handler (int sig)
{
    XENOMAI_SYSCALL1(__xn_sys_migrate,XENOMAI_XENO_DOMAIN);
}

static void *__pthread_trampoline (void *arg)

{
    struct pthread_iargs *iargs = (struct pthread_iargs *)arg;
    void *(*start) (void *), *cookie;
    pthread_t tid = pthread_self();
    struct sched_param param;
    void *status = NULL;
    long err;

    signal(SIGCHLD,&__pthread_sigharden_handler);

    /* Broken pthread libs ignore some of the thread attribute specs
       passed to pthread_create(3), so we force the scheduling policy
       once again here. */
    param.sched_priority = iargs->prio;
    __real_pthread_setschedparam(tid,SCHED_FIFO,&param);

    /* Do _not_ inline the call to pthread_self() in the syscall
       macro: this trashes the syscall regs on some archs. */
    err = XENOMAI_SKINCALL1(__pse51_muxid,
			    __pse51_thread_create,
			    tid);
    iargs->ret = -err;

    /* We must save anything we'll need to use from *iargs on our own
       stack now before posting the sync sema4, since our released
       parent could unwind the stack space onto which the iargs struct
       is laid on before we actually get the CPU back. */

    start = iargs->start;
    cookie = iargs->arg;

    __real_sem_post(&iargs->sync);

    if (!err)
	{
	XENOMAI_SYSCALL1(__xn_sys_migrate,XENOMAI_XENO_DOMAIN);
	status = start(cookie);
	}
    else
	status = (void *)-err;

    pthread_exit(status);
}

int __wrap_pthread_create (pthread_t *tid,
			   const pthread_attr_t *attr,
			   void *(*start) (void *),
			   void *arg)
{
    struct pthread_iargs iargs;
    int inherit, policy, err;
    struct sched_param param;

    /* Run the vanilla pthread_create(3) service whenever SCHED_FIFO
       is not the new thread's policy. */

    if (!attr ||
	(!pthread_attr_getinheritsched(attr,&inherit) &&
	 ((inherit == PTHREAD_EXPLICIT_SCHED &&
	   !pthread_attr_getschedpolicy(attr,&policy) &&
	   !pthread_attr_getschedparam(attr,&param) &&
	   policy != SCHED_FIFO) ||
	  (inherit == PTHREAD_INHERIT_SCHED &&
	   !pthread_getschedparam(pthread_self(),&policy,&param) &&
	   policy != SCHED_FIFO))))
	return __real_pthread_create(tid,attr,start,arg);

    /* Ok, we are about to create a new real-time thread. First start
       a native POSIX thread, then associate a Xenomai shadow to
       it. */

    iargs.start = start;
    iargs.arg = arg;
    iargs.prio = param.sched_priority;
    iargs.ret = EAGAIN;
    __real_sem_init(&iargs.sync,0,0);

    err = __real_pthread_create(tid,attr,&__pthread_trampoline,&iargs);
    if (!err)
	while (__real_sem_wait(&iargs.sync) && errno == EINTR)
	    ;
    __real_sem_destroy(&iargs.sync);

    return err ?: iargs.ret;
}

int __wrap_pthread_detach (pthread_t thread)

{
    return -XENOMAI_SKINCALL1(__pse51_muxid,
			      __pse51_thread_detach,
			      thread);
}

int __wrap_pthread_setschedparam (pthread_t thread,
				  int policy,
				  const struct sched_param *param)
{
    pthread_t myself = pthread_self();
    int err, promoted;

    err = -XENOMAI_SKINCALL5(__pse51_muxid,
			     __pse51_thread_setschedparam,
			     thread,
			     policy,
			     param,
			     myself,
			     &promoted);
    if (!err && promoted)
	{
	signal(SIGCHLD,&__pthread_sigharden_handler);
	XENOMAI_SYSCALL1(__xn_sys_migrate,XENOMAI_XENO_DOMAIN);
	}

    return err;
}

int __wrap_sched_yield (void)

{
    return -XENOMAI_SKINCALL0(__pse51_muxid,
			      __pse51_sched_yield);
}

int __wrap_pthread_yield (void)

{
    __wrap_sched_yield();
    return 0;
}

int pthread_make_periodic_np (pthread_t thread,
			      struct timespec *starttp,
			      struct timespec *periodtp)
{
    return -XENOMAI_SKINCALL3(__pse51_muxid,
			      __pse51_thread_make_periodic,
			      thread,
			      starttp,
			      periodtp);
}

int pthread_wait_np (void)

{
    return -XENOMAI_SKINCALL0(__pse51_muxid,
			      __pse51_thread_wait);
}

int pthread_set_mode_np (int clrmask,
			 int setmask)
{
    pthread_t tid = pthread_self();
    return -XENOMAI_SKINCALL3(__pse51_muxid,
			      __pse51_thread_set_mode,
			      tid, /* Do not inline. */
			      clrmask,
			      setmask);
}

int pthread_set_name_np (pthread_t thread,
			 const char *name)
{
    return -XENOMAI_SKINCALL2(__pse51_muxid,
			      __pse51_thread_set_name,
			      thread,
			      name);
}

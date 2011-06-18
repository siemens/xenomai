/*
 * Copyright (C) 2006 Philippe Gerum <rpm@xenomai.org>.
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
#include <stdio.h>
#include <memory.h>
#include <malloc.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <nucleus/sched.h>
#include <vrtx/vrtx.h>
#include <asm-generic/bits/sigshadow.h>
#include <asm-generic/bits/current.h>
#include <asm-generic/stack.h>
#include "wrappers.h"

#ifdef HAVE___THREAD
__thread TCB __vrtx_tcb __attribute__ ((tls_model ("initial-exec")));
#else /* !HAVE___THREAD */
extern pthread_key_t __vrtx_tskey;
#endif /* !HAVE___THREAD */

extern int __vrtx_muxid;

/* Public Xenomai interface. */

struct vrtx_task_iargs {
	int tid;
	int prio;
	int mode;
	void (*entry) (void *);
	void *param;
	sem_t sync;
};

static int vrtx_task_set_posix_priority(int prio, struct sched_param *param)
{
	int maxpprio, pprio;

	maxpprio = sched_get_priority_max(SCHED_FIFO);

	/* We need to normalize this value first. */
	pprio = vrtx_normalized_prio(prio);
	if (pprio > maxpprio)
		pprio = maxpprio;

	memset(param, 0, sizeof(*param));
	param->sched_priority = pprio;

	return pprio ? SCHED_FIFO : SCHED_OTHER;
}

static void *vrtx_task_trampoline(void *cookie)
{
	struct vrtx_task_iargs *iargs = cookie;
 	void (*entry)(void *arg), *arg;
	struct vrtx_arg_bulk bulk;
	unsigned long mode_offset;
	long err;
#ifndef HAVE___THREAD
	TCB *tcb;
#endif /* !HAVE___THREAD */

	/* vrtx_task_delete requires asynchronous cancellation */
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

#ifndef HAVE___THREAD
	tcb = malloc(sizeof(*tcb));
	if (tcb == NULL) {
		fprintf(stderr, "Xenomai: failed to allocate local TCB?!\n");
		err = -ENOMEM;
		goto fail;
	}

	pthread_setspecific(__vrtx_tskey, tcb);
#endif /* !HAVE___THREAD */

	xeno_sigshadow_install_once();

	bulk.a1 = (u_long)iargs->tid;
	bulk.a2 = (u_long)iargs->prio;
	bulk.a3 = (u_long)iargs->mode;
	bulk.a4 = (u_long)&mode_offset;
	if (bulk.a4 == 0) {
		err = -ENOMEM;
		goto fail;
	}

 	err = XENOMAI_SKINCALL2(__vrtx_muxid, __vrtx_tecreate,
 				&bulk, &iargs->tid);

 	/* Prevent stale memory access after our parent is released. */
 	entry = iargs->entry;
 	arg = iargs->param;
 	__real_sem_post(&iargs->sync);

  	if (err == 0) {
	  xeno_set_current();
	  xeno_set_current_mode(mode_offset);
	  entry(arg);
	}
fail:
	return (void *)err;
}

int sc_tecreate(void (*entry) (void *),
		int tid,
		int prio,
		int mode,
		u_long ustacksz,
		u_long sstacksz __attribute__ ((unused)),
		char *paddr, u_long psize, int *errp)
{
	struct vrtx_task_iargs iargs;
	struct sched_param param;
	pthread_attr_t thattr;
	int err, policy;
	pthread_t thid;

	/* Migrate this thread to the Linux domain since we are about to
	   issue a series of regular kernel syscalls in order to create
	   the new Linux thread, which in turn will be mapped to a VRTX
	   shadow. */

	XENOMAI_SYSCALL1(__xn_sys_migrate, XENOMAI_LINUX_DOMAIN);

	iargs.tid = tid;
	iargs.prio = prio;
	iargs.mode = mode;
	iargs.entry = entry;
	iargs.param = paddr;
	__real_sem_init(&iargs.sync, 0, 0);

	pthread_attr_init(&thattr);

	ustacksz = xeno_stacksize(ustacksz);

	pthread_attr_setinheritsched(&thattr, PTHREAD_EXPLICIT_SCHED);
	policy = vrtx_task_set_posix_priority(prio, &param);
	pthread_attr_setschedparam(&thattr, &param);
	pthread_attr_setschedpolicy(&thattr, policy);
	pthread_attr_setstacksize(&thattr, ustacksz);
	pthread_attr_setdetachstate(&thattr, PTHREAD_CREATE_DETACHED);

	err = __real_pthread_create(&thid, &thattr, &vrtx_task_trampoline, &iargs);
	if (err) {
		*errp = err;
		__real_sem_destroy(&iargs.sync);
		return -1;
	}

	while (__real_sem_wait(&iargs.sync) && errno == EINTR) ;
	__real_sem_destroy(&iargs.sync);

	return iargs.tid;
}

int sc_tcreate(void (*entry) (void *), int tid, int prio, int *errp)
{
	return sc_tecreate(entry, tid, prio, 0, 0, 0, NULL, 0, errp);
	/* Eh, this one was easy. */
}

void sc_tdelete(int tid, int opt, int *errp)
{
	*errp = XENOMAI_SKINCALL2(__vrtx_muxid, __vrtx_tdelete, tid, opt);
}

void sc_tpriority(int tid, int prio, int *errp)
{
	*errp = XENOMAI_SKINCALL2(__vrtx_muxid, __vrtx_tpriority, tid, prio);
}

void sc_tresume(int tid, int opt, int *errp)
{
	*errp = XENOMAI_SKINCALL2(__vrtx_muxid, __vrtx_tresume, tid, opt);
}

void sc_tsuspend(int tid, int opt, int *errp)
{
	*errp = XENOMAI_SKINCALL2(__vrtx_muxid, __vrtx_tsuspend, tid, opt);
}

TCB *sc_tinquiry(int pinfo[], int tid, int *errp)
{
	TCB *tcb;

#ifdef HAVE___THREAD
	tcb = &__vrtx_tcb;
#else /* !HAVE___THREAD */
	tcb = (TCB *) pthread_getspecific(__vrtx_tskey); /* Cannot fail. */
#endif /* !HAVE___THREAD */

	*errp = XENOMAI_SKINCALL3(__vrtx_muxid,
				  __vrtx_tinquiry, pinfo, tcb, tid);
	if (*errp)
		return NULL;

	return tcb;
}

void sc_tslice(unsigned short ticks)
{
	XENOMAI_SKINCALL1(__vrtx_muxid, __vrtx_tslice, ticks);
}

void sc_lock(void)
{
	XENOMAI_SKINCALL0(__vrtx_muxid, __vrtx_lock);
}

void sc_unlock(void)
{
	XENOMAI_SKINCALL0(__vrtx_muxid, __vrtx_unlock);
}

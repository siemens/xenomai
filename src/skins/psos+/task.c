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
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <memory.h>
#include <string.h>
#include <psos+/psos.h>
#include <psos+/long_names.h>
#include <asm-generic/bits/sigshadow.h>
#include <asm-generic/bits/current.h>
#include <asm-generic/stack.h>

extern int __psos_muxid;

struct psos_task_iargs {

	const char *name;
	u_long prio;
	u_long flags;
	u_long *tid_r;
	xncompletion_t *completionp;
};

static int psos_task_set_posix_priority(int prio, struct sched_param *param)
{
	int maxpprio, pprio;

	maxpprio = sched_get_priority_max(SCHED_FIFO);

	/* We need to normalize this value first. */
	pprio = psos_normalized_prio(prio);
	if (pprio > maxpprio)
		pprio = maxpprio;

	memset(param, 0, sizeof(*param));
	param->sched_priority = pprio;

	return pprio ? SCHED_FIFO : SCHED_OTHER;
}

static void *psos_task_trampoline(void *cookie)
{
	struct psos_task_iargs *iargs = (struct psos_task_iargs *)cookie;
	void (*entry)(u_long, u_long, u_long, u_long);
	volatile pthread_t tid = pthread_self();
	struct psos_arg_bulk bulk;
	unsigned long mode_offset;
	u_long handle, targs[4];
	long err;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	xeno_sigshadow_install_once();

	bulk.a1 = (u_long)iargs->name;
	bulk.a2 = (u_long)iargs->prio;
	bulk.a3 = (u_long)iargs->flags;
	bulk.a4 = (u_long)&mode_offset;
	bulk.a5 = (u_long)tid;

	if (!bulk.a4) {
		err = -ENOMEM;
		goto fail;
	}

	err = XENOMAI_SKINCALL3(__psos_muxid,
				__psos_t_create,
				&bulk, iargs->tid_r, iargs->completionp);
	if (err)
		goto fail;

	xeno_set_current();
	xeno_set_current_mode(mode_offset);

	/* Wait on the barrier for the task to be started. The barrier
	   could be released in order to process Linux signals while the
	   Xenomai shadow is still dormant; in such a case, resume wait. */

	do
		err = XENOMAI_SYSCALL2(__xn_sys_barrier, &entry, &handle);
	while (err == -EINTR);
	if (err)
		goto fail;

	err = XENOMAI_SKINCALL2(__psos_muxid,
				__psos_t_getargs, handle, targs);
	if (err)
		goto fail;

	entry(targs[0], targs[1], targs[2], targs[3]);

      fail:

	return (void *)err;
}

u_long t_create(const char *name,
		u_long prio,
		u_long sstack,	/* Ignored. */
		u_long ustack,
		u_long flags,
		u_long *tid_r)
{
	struct psos_task_iargs iargs;
	xncompletion_t completion;
	struct sched_param param;
	pthread_attr_t thattr;
	char short_name[5];
	pthread_t thid;
	int policy;
	long err;

	name = __psos_maybe_short_name(short_name, name);

	/* Migrate this thread to the Linux domain since we are about
	   to issue a series of regular kernel syscalls in order to
	   create the new Linux thread, which in turn will be mapped
	   to a pSOS shadow. */

	XENOMAI_SYSCALL1(__xn_sys_migrate, XENOMAI_LINUX_DOMAIN);

	completion.syncflag = 0;
	completion.pid = -1;

	iargs.name = name;
	iargs.prio = prio;
	iargs.flags = flags;
	iargs.tid_r = tid_r;
	iargs.completionp = &completion;

	pthread_attr_init(&thattr);

	ustack += sstack;

	ustack = xeno_stacksize(ustack);

	pthread_attr_setinheritsched(&thattr, PTHREAD_EXPLICIT_SCHED);
	policy = psos_task_set_posix_priority(prio, &param);
	pthread_attr_setschedpolicy(&thattr, policy);
	pthread_attr_setschedparam(&thattr, &param);
	pthread_attr_setstacksize(&thattr, ustack);
	pthread_attr_setdetachstate(&thattr, PTHREAD_CREATE_DETACHED);

	err = pthread_create(&thid, &thattr, &psos_task_trampoline, &iargs);

	/* Pass back POSIX codes returned by internal calls as
	   negative values to distinguish them from pSOS ones. */

	if (err)
		return -err;

	/* Sync with psos_task_trampoline() then return.*/

	return XENOMAI_SYSCALL1(__xn_sys_completion, &completion);
}

u_long t_shadow(const char *name, /* Xenomai extension. */
		u_long prio,
		u_long flags,
		u_long *tid_r)
{
	struct psos_arg_bulk bulk;
	unsigned long mode_offset;
	int ret;

	xeno_fault_stack();

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	xeno_sigshadow_install_once();

	bulk.a1 = (u_long)name;
	bulk.a2 = (u_long)prio;
	bulk.a3 = (u_long)flags;
	bulk.a4 = (u_long)&mode_offset;
	bulk.a5 = (u_long)pthread_self();

	ret = XENOMAI_SKINCALL3(__psos_muxid, __psos_t_create, &bulk, tid_r, NULL);
	if (!ret) {
		xeno_set_current();
		xeno_set_current_mode(mode_offset);
	}

	return ret;
}

u_long t_start(u_long tid,
	       u_long mode,
	       void (*startaddr)(u_long a0,
				 u_long a1,
				 u_long a2,
				 u_long a3),
	       u_long targs[])
{
	return XENOMAI_SKINCALL4(__psos_muxid, __psos_t_start,
				 tid, mode, startaddr, targs);
}

u_long t_delete(u_long tid)
{
	u_long ptid;
	long err;

	if (tid == 0)
		goto self_delete;

	err = XENOMAI_SKINCALL2(__psos_muxid, __psos_t_getpth, tid, &ptid);
	if (err)
		return err;

	if ((pthread_t)ptid == pthread_self())
		goto self_delete;

	err = pthread_cancel((pthread_t)ptid);
	if (err)
		return -err; /* differentiate from pSOS codes */

	err = XENOMAI_SKINCALL1(__psos_muxid, __psos_t_delete, tid);
	if (err == ERR_OBJID)
		return SUCCESS;

	return err;

self_delete:

	 /* Silently migrate to avoid raising SIGXCPU. */
	XENOMAI_SYSCALL1(__xn_sys_migrate, XENOMAI_LINUX_DOMAIN);
	pthread_exit(NULL);

	return SUCCESS; /* not reached */
}

u_long t_suspend(u_long tid)
{
	return XENOMAI_SKINCALL1(__psos_muxid, __psos_t_suspend, tid);
}

u_long t_resume(u_long tid)
{
	return XENOMAI_SKINCALL1(__psos_muxid, __psos_t_resume, tid);
}

u_long t_setreg(u_long tid, u_long regnum, u_long regvalue)
{
	return XENOMAI_SKINCALL3(__psos_muxid, __psos_t_setreg,
				 tid, regnum, regvalue);
}

u_long t_getreg(u_long tid, u_long regnum, u_long *regvalue_r)
{
	return XENOMAI_SKINCALL3(__psos_muxid, __psos_t_getreg,
				 tid, regnum, regvalue_r);
}

u_long t_ident(const char *name, u_long nodeno, u_long *tid_r)
{
	char short_name[5];

	name = __psos_maybe_short_name(short_name, name);

	return XENOMAI_SKINCALL2(__psos_muxid, __psos_t_ident, name, tid_r);
}

u_long t_mode(u_long clrmask, u_long setmask, u_long *oldmode_r)
{
	return XENOMAI_SKINCALL3(__psos_muxid, __psos_t_mode,
				 clrmask, setmask, oldmode_r);
}

u_long t_setpri(u_long tid, u_long newprio, u_long *oldprio_r)
{
	return XENOMAI_SKINCALL3(__psos_muxid, __psos_t_setpri,
				 tid, newprio, oldprio_r);
}

u_long ev_send(u_long tid, u_long events)
{
	return XENOMAI_SKINCALL2(__psos_muxid, __psos_ev_send, tid, events);
}

u_long ev_receive(u_long events, u_long flags, u_long timeout, u_long *events_r)
{
	return XENOMAI_SKINCALL4(__psos_muxid, __psos_ev_receive,
				 events, flags, timeout, events_r);
}

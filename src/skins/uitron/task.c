/*
 * Copyright (C) 2007 Philippe Gerum <rpm@xenomai.org>.
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

#include <asm/xenomai/system.h>
#include <asm-generic/bits/sigshadow.h>
#include <asm-generic/bits/current.h>
#include <asm-generic/stack.h>
#include <nucleus/sched.h>
#include <uitron/uitron.h>

extern int __uitron_muxid;

struct uitron_task_iargs {

	ID tskid;
	T_CTSK *pk_ctsk;
	xncompletion_t *completionp;
};

static int uitron_task_set_posix_priority(int prio, struct sched_param *param)
{
	int maxpprio, pprio;

	maxpprio = sched_get_priority_max(SCHED_FIFO);

	/* We need to normalize this value first. */
	pprio = ui_normalized_prio(prio);
	if (pprio > maxpprio)
		pprio = maxpprio;

	memset(param, 0, sizeof(*param));
	param->sched_priority = pprio;

	return pprio ? SCHED_FIFO : SCHED_OTHER;
}

static void *uitron_task_trampoline(void *cookie)
{
	struct uitron_task_iargs *iargs = (struct uitron_task_iargs *)cookie;
	struct sched_param param;
	unsigned long mode_offset;
	void (*entry)(INT);
	int policy;
	long err;
	INT arg;

	/*
	 * Apply sched params here as some libpthread implementations
	 * fail doing this properly via pthread_create.
	 */
	policy = uitron_task_set_posix_priority(iargs->pk_ctsk->itskpri, &param);
	pthread_setschedparam(pthread_self(), policy, &param);

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	xeno_sigshadow_install_once();

	err = XENOMAI_SKINCALL4(__uitron_muxid,
				__uitron_cre_tsk,
				iargs->tskid, iargs->pk_ctsk,
				iargs->completionp, &mode_offset);
	if (err)
		goto fail;

	xeno_set_current();
	xeno_set_current_mode(mode_offset);

	/* iargs->pk_ctsk might not be valid anymore, after our parent
	   was released from the completion sync, so do not
	   dereference this pointer. */

	do
		err = XENOMAI_SYSCALL2(__xn_sys_barrier, &entry, &arg);
	while (err == -EINTR);

	if (!err)
		entry(arg);

      fail:

	return (void *)err;
}

ER cre_tsk(ID tskid, T_CTSK *pk_ctsk)
{
	struct uitron_task_iargs iargs;
	xncompletion_t completion;
	struct sched_param param;
	pthread_attr_t thattr;
	pthread_t thid;
	int policy;
	long err;

	XENOMAI_SYSCALL1(__xn_sys_migrate, XENOMAI_LINUX_DOMAIN);

	completion.syncflag = 0;
	completion.pid = -1;

	iargs.tskid = tskid;
	iargs.pk_ctsk = pk_ctsk;
	iargs.completionp = &completion;

	pthread_attr_init(&thattr);

	pk_ctsk->stksz = xeno_stacksize(pk_ctsk->stksz);

	pthread_attr_setinheritsched(&thattr, PTHREAD_EXPLICIT_SCHED);
	policy = uitron_task_set_posix_priority(pk_ctsk->itskpri, &param);
	pthread_attr_setschedparam(&thattr, &param);
	pthread_attr_setschedpolicy(&thattr, policy);
	pthread_attr_setstacksize(&thattr, pk_ctsk->stksz);
	pthread_attr_setdetachstate(&thattr, PTHREAD_CREATE_DETACHED);

	err = pthread_create(&thid, &thattr, &uitron_task_trampoline, &iargs);
	if (err)
		return -err;

	/* Sync with uitron_task_trampoline() then return.*/

	return XENOMAI_SYSCALL1(__xn_sys_completion, &completion);
}

ER shd_tsk(ID tskid, T_CTSK *pk_ctsk) /* Xenomai extension. */
{
	struct sched_param param;
	int policy, err;

	xeno_fault_stack();

	/* Make sure the POSIX library caches the right priority. */
	policy = uitron_task_set_posix_priority(pk_ctsk->itskpri, &param);
	pthread_setschedparam(pthread_self(), policy, &param);

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	xeno_sigshadow_install_once();

	err = XENOMAI_SKINCALL3(__uitron_muxid,
				__uitron_cre_tsk,
				tskid, pk_ctsk,
				NULL);

	if (!err)
		xeno_set_current();

	return err;
}

ER del_tsk(ID tskid)
{
	return XENOMAI_SKINCALL1(__uitron_muxid, __uitron_del_tsk, tskid);
}

ER sta_tsk(ID tskid, INT stacd)
{
	return XENOMAI_SKINCALL2(__uitron_muxid, __uitron_sta_tsk,
				 tskid, stacd);
}

void ext_tsk(void)
{
	XENOMAI_SKINCALL0(__uitron_muxid, __uitron_ext_tsk);
}

void exd_tsk(void)
{
	XENOMAI_SKINCALL0(__uitron_muxid, __uitron_exd_tsk);
}

ER ter_tsk(ID tskid)
{
	return XENOMAI_SKINCALL1(__uitron_muxid, __uitron_ter_tsk, tskid);
}

ER dis_dsp(void)
{
	return XENOMAI_SKINCALL0(__uitron_muxid, __uitron_dis_dsp);
}

ER ena_dsp(void)
{
	return XENOMAI_SKINCALL0(__uitron_muxid, __uitron_ena_dsp);
}

ER chg_pri(ID tskid, PRI tskpri)
{
	return XENOMAI_SKINCALL2(__uitron_muxid, __uitron_chg_pri,
				 tskid, tskpri);
}

ER rot_rdq(PRI tskpri)
{
	return XENOMAI_SKINCALL1(__uitron_muxid, __uitron_rot_rdq, tskpri);
}

ER rel_wai(ID tskid)
{
	return XENOMAI_SKINCALL1(__uitron_muxid, __uitron_rel_wai, tskid);
}

ER get_tid(ID *p_tskid)
{
	return XENOMAI_SKINCALL1(__uitron_muxid, __uitron_get_tid, p_tskid);
}

ER ref_tsk(T_RTSK *pk_rtsk, ID tskid)
{
	return XENOMAI_SKINCALL2(__uitron_muxid, __uitron_ref_tsk,
				 pk_rtsk, tskid);
}

ER sus_tsk(ID tskid)
{
	return XENOMAI_SKINCALL1(__uitron_muxid, __uitron_sus_tsk, tskid);
}

ER rsm_tsk(ID tskid)
{
	return XENOMAI_SKINCALL1(__uitron_muxid, __uitron_rsm_tsk, tskid);
}

ER frsm_tsk(ID tskid)
{
	return XENOMAI_SKINCALL1(__uitron_muxid, __uitron_frsm_tsk, tskid);
}

ER slp_tsk(void)
{
	return XENOMAI_SKINCALL0(__uitron_muxid, __uitron_slp_tsk);
}

ER tslp_tsk(TMO tmout)
{
	return XENOMAI_SKINCALL1(__uitron_muxid, __uitron_tslp_tsk, tmout);
}

ER wup_tsk(ID tskid)
{
	return XENOMAI_SKINCALL1(__uitron_muxid, __uitron_wup_tsk, tskid);
}

ER can_wup(INT *p_wupcnt, ID tskid)
{
	return XENOMAI_SKINCALL2(__uitron_muxid, __uitron_can_wup,
				 p_wupcnt, tskid);
}

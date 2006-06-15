/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Julien Pinon <jpinon@idealx.com>.
 * Copyright (C) 2003,2006 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <vrtx/task.h>
#include <vrtx/sem.h>

static vrtxidmap_t *vrtx_sem_idmap;

static xnqueue_t vrtx_sem_q;

static int sem_destroy_internal(vrtxsem_t *sem)
{
	int s;

	removeq(&vrtx_sem_q, &sem->link);
	vrtx_put_id(vrtx_sem_idmap, sem->semid);
	s = xnsynch_destroy(&sem->synchbase);
	vrtx_mark_deleted(sem);
	xnfree(sem);

	return s;
}

int vrtxsem_init(void)
{
	initq(&vrtx_sem_q);
	vrtx_sem_idmap = vrtx_alloc_idmap(VRTX_MAX_SEMS, 0);
	return vrtx_sem_idmap ? 0 : -ENOMEM;
}

void vrtxsem_cleanup(void)
{

	xnholder_t *holder;

	while ((holder = getheadq(&vrtx_sem_q)) != NULL)
		sem_destroy_internal(link2vrtxsem(holder));

	vrtx_free_idmap(vrtx_sem_idmap);
}

int sc_screate(unsigned initval, int opt, int *errp)
{
	int bflags = 0, semid;
	vrtxsem_t *sem;
	spl_t s;

	if (opt & ~1) {
		*errp = ER_IIP;
		return -1;
	}

	sem = (vrtxsem_t *)xnmalloc(sizeof(*sem));

	if (!sem) {
		*errp = ER_NOCB;
		return -1;
	}

	semid = vrtx_get_id(vrtx_sem_idmap, -1, sem);

	if (semid < 0) {
		*errp = ER_NOCB;
		xnfree(sem);
		return -1;
	}

	if (opt == 0)
		bflags = XNSYNCH_PRIO;
	else
		bflags = XNSYNCH_FIFO;

	xnsynch_init(&sem->synchbase, bflags | XNSYNCH_DREORD);
	inith(&sem->link);
	sem->semid = semid;
	sem->magic = VRTX_SEM_MAGIC;
	sem->count = initval;

	xnlock_get_irqsave(&nklock, s);
	appendq(&vrtx_sem_q, &sem->link);
	xnlock_put_irqrestore(&nklock, s);

	*errp = RET_OK;

	return semid;
}

void sc_sdelete(int semid, int opt, int *errp)
{
	vrtxsem_t *sem;
	spl_t s;

	if (opt & ~1) {
		*errp = ER_IIP;
		return;
	}

	xnlock_get_irqsave(&nklock, s);

	sem = (vrtxsem_t *)vrtx_get_object(vrtx_sem_idmap, semid);

	if (sem == NULL) {
		*errp = ER_ID;
		goto unlock_and_exit;
	}

	if (opt == 0 && xnsynch_nsleepers(&sem->synchbase) > 0) {
		*errp = ER_PND;
		goto unlock_and_exit;
	}

	/* forcing delete or no task pending */
	if (sem_destroy_internal(sem) == XNSYNCH_RESCHED)
		xnpod_schedule();

	*errp = RET_OK;

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);
}

void sc_spend(int semid, long timeout, int *errp)
{
	vrtxtask_t *task;
	vrtxsem_t *sem;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	sem = (vrtxsem_t *)vrtx_get_object(vrtx_sem_idmap, semid);

	if (sem == NULL) {
		*errp = ER_ID;
		goto unlock_and_exit;
	}

	*errp = RET_OK;

	if (sem->count > 0)
		sem->count--;
	else {
		if (xnpod_unblockable_p()) {
			*errp = -EPERM;
			goto unlock_and_exit;
		}

		task = vrtx_current_task();

		task->vrtxtcb.TCBSTAT = TBSSEMA;

		if (timeout)
			task->vrtxtcb.TCBSTAT |= TBSDELAY;

		xnsynch_sleep_on(&sem->synchbase, timeout);

		if (xnthread_test_flags(&task->threadbase, XNBREAK))
			*errp = -EINTR;
		else if (xnthread_test_flags(&task->threadbase, XNRMID))
			*errp = ER_DEL;	/* Semaphore deleted while pending. */
		else if (xnthread_test_flags(&task->threadbase, XNTIMEO))
			*errp = ER_TMO;	/* Timeout. */
	}

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);
}

void sc_saccept(int semid, int *errp)
{
	vrtxsem_t *sem;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	sem = (vrtxsem_t *)vrtx_get_object(vrtx_sem_idmap, semid);

	if (sem == NULL) {
		*errp = ER_ID;
		goto unlock_and_exit;
	}

	if (sem->count > 0) {
		sem->count--;
		*errp = RET_OK;
	} else
		*errp = ER_NMP;

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);
}

void sc_spost(int semid, int *errp)
{
	vrtxsem_t *sem;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	sem = (vrtxsem_t *)vrtx_get_object(vrtx_sem_idmap, semid);

	if (sem == NULL) {
		*errp = ER_ID;
		goto unlock_and_exit;
	}

	*errp = RET_OK;

	if (xnsynch_wakeup_one_sleeper(&sem->synchbase))
		xnpod_schedule();
	else if (sem->count == MAX_SEM_VALUE)
		*errp = ER_OVF;
	else
		sem->count++;

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);
}

int sc_sinquiry(int semid, int *errp)
{
	vrtxsem_t *sem;
	int count;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	sem = (vrtxsem_t *)vrtx_get_object(vrtx_sem_idmap, semid);

	if (sem == NULL) {
		*errp = ER_ID;
		count = 0;
	} else {
		*errp = RET_OK;
		count = sem->count;
	}

	xnlock_put_irqrestore(&nklock, s);

	return count;
}

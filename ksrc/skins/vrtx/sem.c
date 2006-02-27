/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Julien Pinon <jpinon@idealx.com>.
 * Copyright (C) 2003 Philippe Gerum <rpm@xenomai.org>.
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

#include "vrtx/task.h"
#include "vrtx/sem.h"

static xnqueue_t vrtxsemq;

static int sem_destroy_internal(vrtxsem_t *sem);

void vrtxsem_init (void) {
    initq(&vrtxsemq);
}

void vrtxsem_cleanup (void) {

    xnholder_t *holder;

    while ((holder = getheadq(&vrtxsemq)) != NULL)
	sem_destroy_internal(link2vrtxsem(holder));
}

static int sem_destroy_internal (vrtxsem_t *sem)

{
    spl_t s;
    int rc;

    xnlock_get_irqsave(&nklock,s);
    removeq(&vrtxsemq,&sem->link);
    vrtx_release_id(sem->semid);
    rc = xnsynch_destroy(&sem->synchbase);
    vrtx_mark_deleted(sem);
    xnlock_put_irqrestore(&nklock,s);

    xnfree(sem);

    return rc;
}

int sc_screate (unsigned initval, int opt, int *perr)

{
    int bflags = 0, semid;
    vrtxsem_t *sem;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);
    if ((opt != 1) && (opt != 0))
	{
	*perr = ER_IIP;
	return 0;
	}

    sem = (vrtxsem_t *)xnmalloc(sizeof(*sem));

    if (!sem)
	{
	*perr = ER_NOCB;
	return 0;
	}

    semid = vrtx_alloc_id(sem);

    if (semid < 0)
	{
	*perr = ER_NOCB;
	xnfree(sem);
	return 0;
	}

    if (opt == 0)
	bflags = XNSYNCH_PRIO;
    else
	bflags = XNSYNCH_FIFO;

    xnsynch_init(&sem->synchbase,bflags|XNSYNCH_DREORD);
    inith(&sem->link);
    sem->semid = semid;
    sem->magic = VRTX_SEM_MAGIC;
    sem->count = initval;

    xnlock_get_irqsave(&nklock,s);
    appendq(&vrtxsemq,&sem->link);
    xnlock_put_irqrestore(&nklock,s);

    *perr = RET_OK;

    return semid;
}

void sc_sdelete(int semid, int opt, int *errp)
{
    vrtxsem_t *sem;
    spl_t s;

    if ((opt != 0) && (opt != 1))
	{
	*errp = ER_IIP;
	return;
	}

    xnlock_get_irqsave(&nklock,s);

    sem = (vrtxsem_t *)vrtx_find_object_by_id(semid);
    if (sem == NULL)
	{
	xnlock_put_irqrestore(&nklock,s);
	*errp = ER_ID;
	return;
	}

    *errp = RET_OK;

    if (opt == 0 && xnsynch_nsleepers(&sem->synchbase) > 0)
	{
	xnlock_put_irqrestore(&nklock,s);
	*errp = ER_PND;
	return;
	}

    /* forcing delete or no task pending */
    if (sem_destroy_internal(sem) == XNSYNCH_RESCHED)
	xnpod_schedule();

    xnlock_put_irqrestore(&nklock,s);
}    

void sc_spend(int semid, long timeout, int *errp)
{
    vrtxsem_t *sem;
    vrtxtask_t *task;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnlock_get_irqsave(&nklock,s);

    sem = (vrtxsem_t *)vrtx_find_object_by_id(semid);
    if (sem == NULL)
	{
	xnlock_put_irqrestore(&nklock,s);
	*errp = ER_ID;
	return;
	}

    *errp = RET_OK;
    if (sem->count > 0)
	sem->count--;
    else
	{
	task = vrtx_current_task();
	task->vrtxtcb.TCBSTAT = TBSSEMA;
	if (timeout)
	    task->vrtxtcb.TCBSTAT |= TBSDELAY;
	xnsynch_sleep_on(&sem->synchbase,timeout);

	if (xnthread_test_flags(&task->threadbase, XNRMID))
	    *errp = ER_DEL; /* Semaphore deleted while pending. */
	else if (xnthread_test_flags(&task->threadbase, XNTIMEO))
	    *errp = ER_TMO; /* Timeout.*/
	}

    xnlock_put_irqrestore(&nklock,s);
}

void sc_saccept(int semid, int *errp)
{
    vrtxsem_t *sem;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    sem = (vrtxsem_t *)vrtx_find_object_by_id(semid);
    if (sem == NULL)
	{
	xnlock_put_irqrestore(&nklock,s);
	*errp = ER_ID;
	return;
	}

    if (sem->count > 0)
	{
	sem->count--;
	}
    else
	{
	*errp = ER_NMP;
	}

    xnlock_put_irqrestore(&nklock,s);
}

void sc_spost(int semid, int *errp)
{
    xnthread_t *waiter;
    vrtxsem_t *sem;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);
    sem = (vrtxsem_t *)vrtx_find_object_by_id(semid);
    if (sem == NULL)
	{
	xnlock_put_irqrestore(&nklock,s);
	*errp = ER_ID;
	return;
	}

    *errp = RET_OK;

    waiter = xnsynch_wakeup_one_sleeper(&sem->synchbase);

    if (waiter)
	{
	xnpod_schedule();
	}
    else
	{
	if (sem->count == MAX_SEM_VALUE)
	    {
	    *errp = ER_OVF;
	    }
	else
	    {
	    sem->count++;
	    }
	}

    xnlock_put_irqrestore(&nklock,s);
}

int sc_sinquiry (int semid, int *errp)
{
    vrtxsem_t *sem;
    int count;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    sem = (vrtxsem_t *)vrtx_find_object_by_id(semid);
    if (sem == NULL)
	{
	*errp = ER_ID;
	count = 0;
	}
    else
	count = sem->count;

    xnlock_put_irqrestore(&nklock,s);

    *errp = RET_OK;
    
    return count;
}

/*
 * IMPLEMENTATION NOTES:
 *
 * - Code executing on behalf of interrupt context is currently
 * allowed to scan the global vrtx semaphore queue (vrtxsemq).
 */

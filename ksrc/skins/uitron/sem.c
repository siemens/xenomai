/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
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

#include "uitron/task.h"
#include "uitron/sem.h"

static xnqueue_t uisemq;

static uisem_t *uisemmap[uITRON_MAX_SEMID];

void uisem_init (void) {
    initq(&uisemq);
}

void uisem_cleanup (void)

{
    xnholder_t *holder;

    while ((holder = getheadq(&uisemq)) != NULL)
	del_sem(link2uisem(holder)->semid);
}

ER cre_sem (ID semid, T_CSEM *pk_csem)

{
    uisem_t *sem;
    spl_t s;

    if (xnpod_asynch_p())
	return EN_CTXID;

    if (pk_csem->isemcnt < 0 ||
	pk_csem->maxsem < 0 ||
	pk_csem->isemcnt > pk_csem->maxsem)
	return E_PAR;

    if (semid <= 0 || semid > uITRON_MAX_SEMID)
	return E_ID;

    xnlock_get_irqsave(&nklock,s);

    if (uisemmap[semid - 1] != NULL)
	{
	xnlock_put_irqrestore(&nklock,s);
	return E_OBJ;
	}

    uisemmap[semid - 1] = (uisem_t *)1; /* Reserve slot */

    xnlock_put_irqrestore(&nklock,s);

    sem = (uisem_t *)xnmalloc(sizeof(*sem));

    if (!sem)
	{
	uisemmap[semid - 1] = NULL;
	return E_NOMEM;
	}

    xnsynch_init(&sem->synchbase,
		 (pk_csem->sematr & TA_TPRI) ? XNSYNCH_PRIO : XNSYNCH_FIFO);

    inith(&sem->link);
    sem->semid = semid;
    sem->exinf = pk_csem->exinf;
    sem->sematr = pk_csem->sematr;
    sem->semcnt = pk_csem->isemcnt;
    sem->maxsem = pk_csem->maxsem;
    sem->magic = uITRON_SEM_MAGIC;

    xnlock_get_irqsave(&nklock,s);
    uisemmap[semid - 1] = sem;
    appendq(&uisemq,&sem->link);
    xnlock_put_irqrestore(&nklock,s);

    return E_OK;
}

ER del_sem (ID semid)

{
    uisem_t *sem;
    spl_t s;
    
    if (xnpod_asynch_p())
	return EN_CTXID;

    if (semid <= 0 || semid > uITRON_MAX_SEMID)
	return E_ID;

    xnlock_get_irqsave(&nklock,s);

    sem = uisemmap[semid - 1];

    if (!sem)
	{
	xnlock_put_irqrestore(&nklock,s);
	return E_NOEXS;
	}

    uisemmap[semid - 1] = NULL;

    ui_mark_deleted(sem);

    if (xnsynch_destroy(&sem->synchbase) == XNSYNCH_RESCHED)
	xnpod_schedule();

    xnlock_put_irqrestore(&nklock,s);

    xnfree(sem);

    return E_OK;
}

ER sig_sem (ID semid)

{
    uitask_t *sleeper;
    uisem_t *sem;
    int err;
    spl_t s;
    
    if (xnpod_asynch_p())
	return EN_CTXID;

    if (semid <= 0 || semid > uITRON_MAX_SEMID)
	return E_ID;

    xnlock_get_irqsave(&nklock,s);

    sem = uisemmap[semid - 1];

    if (!sem)
	{
	xnlock_put_irqrestore(&nklock,s);
	return E_NOEXS;
	}

    sleeper = thread2uitask(xnsynch_wakeup_one_sleeper(&sem->synchbase));

    if (sleeper)
	{
	xnpod_schedule();
	xnlock_put_irqrestore(&nklock,s);
	return E_OK;
	}

    err = E_OK;

    if (++sem->semcnt > sem->maxsem || sem->semcnt < 0)
	{
	sem->semcnt--;
	err = E_QOVR;
	}

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

static ER wai_sem_helper (ID semid, TMO tmout)

{
    xnticks_t timeout;
    uitask_t *task;
    uisem_t *sem;
    int err;
    spl_t s;
    
    if (xnpod_unblockable_p())
	return E_CTX;

    if (tmout == TMO_FEVR)
	timeout = XN_INFINITE;
    else if (tmout == 0)
	timeout = XN_NONBLOCK;
    else if (tmout < TMO_FEVR)
	return E_PAR;
    else
	timeout = (xnticks_t)tmout;

    if (semid <= 0 || semid > uITRON_MAX_SEMID)
	return E_ID;

    xnlock_get_irqsave(&nklock,s);

    sem = uisemmap[semid - 1];

    if (!sem)
	{
	xnlock_put_irqrestore(&nklock,s);
	return E_NOEXS;
	}

    err = E_OK;

    if (sem->semcnt > 0)
	sem->semcnt--;
    else if (timeout == XN_NONBLOCK)
	err = E_TMOUT;
    else
	{
	task = ui_current_task();

	xnsynch_sleep_on(&sem->synchbase,timeout);

	if (xnthread_test_flags(&task->threadbase,XNRMID))
	    err = E_DLT; /* Semaphore deleted while pending. */
	else if (xnthread_test_flags(&task->threadbase,XNTIMEO))
	    err = E_TMOUT; /* Timeout.*/
	else if (xnthread_test_flags(&task->threadbase,XNBREAK))
	    err = E_RLWAI; /* rel_wai() received while waiting.*/
	}

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

ER wai_sem (ID semid) {

    return wai_sem_helper(semid,TMO_FEVR);
}

ER preq_sem (ID semid) {

    return wai_sem_helper(semid,0);
}

ER twai_sem (ID semid, TMO tmout) {

    return wai_sem_helper(semid,tmout);
}

ER ref_sem (T_RSEM *pk_rsem, ID semid)

{
    uitask_t *sleeper;
    uisem_t *sem;
    spl_t s;
    
    if (semid <= 0 || semid > uITRON_MAX_SEMID)
	return E_ID;

    xnlock_get_irqsave(&nklock,s);

    sem = uisemmap[semid - 1];

    if (!sem)
	{
	xnlock_put_irqrestore(&nklock,s);
	return E_NOEXS;
	}

    sleeper = thread2uitask(link2thread(getheadpq(xnsynch_wait_queue(&sem->synchbase)),plink));
    pk_rsem->exinf = sem->exinf;
    pk_rsem->semcnt = sem->semcnt;
    pk_rsem->wtsk = sleeper ? sleeper->tskid : FALSE;

    xnlock_put_irqrestore(&nklock,s);

    return E_OK;
}

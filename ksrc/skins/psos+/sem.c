/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "xenomai/psos+/task.h"
#include "xenomai/psos+/sem.h"

static xnqueue_t psossemq;

static int sm_destroy_internal(psossem_t *sem);

void psossem_init (void) {
    initq(&psossemq);
}

void psossem_cleanup (void) {

    xnholder_t *holder;

    while ((holder = getheadq(&psossemq)) != NULL)
	sm_destroy_internal(link2psossem(holder));
}

u_long sm_create (char name[4],
		  u_long icount,
		  u_long flags,
		  u_long *smid)
{
    psossem_t *sem;
    int bflags = 0;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    sem = (psossem_t *)xnmalloc(sizeof(*sem));

    if (!sem)
	return ERR_NOSCB;

    if (flags & SM_PRIOR)
	bflags |= XNSYNCH_PRIO;

    xnsynch_init(&sem->synchbase,bflags);

    inith(&sem->link);
    sem->count = icount;
    sem->magic = PSOS_SEM_MAGIC;
    sem->name[0] = name[0];
    sem->name[1] = name[1];
    sem->name[2] = name[2];
    sem->name[3] = name[3];
    sem->name[4] = '\0';

    xnlock_get_irqsave(&nklock,s);
    appendq(&psossemq,&sem->link);
    xnlock_put_irqrestore(&nklock,s);

    *smid = (u_long)sem;

    return SUCCESS;
}

static int sm_destroy_internal (psossem_t *sem)

{
    spl_t s;
    int rc;

    xnlock_get_irqsave(&nklock,s);
    removeq(&psossemq,&sem->link);
    rc = xnsynch_destroy(&sem->synchbase);
    psos_mark_deleted(sem);
    xnlock_put_irqrestore(&nklock,s);

    xnfree(sem);

    return rc;
}

u_long sm_delete (u_long smid)

{
    u_long err = SUCCESS;
    psossem_t *sem;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnlock_get_irqsave(&nklock,s);

    sem = psos_h2obj_active(smid,PSOS_SEM_MAGIC,psossem_t);

    if (!sem)
	{
	err = psos_handle_error(smid,PSOS_SEM_MAGIC,psossem_t);
	goto unlock_and_exit;
	}

    if (sm_destroy_internal(sem) == XNSYNCH_RESCHED)
	xnpod_schedule();

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

u_long sm_ident (char name[4],
		 u_long node,
		 u_long *smid)
{
    u_long err = SUCCESS;
    xnholder_t *holder;
    psossem_t *sem;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if (node > 1)
	return ERR_NODENO;

    xnlock_get_irqsave(&nklock,s);

    for (holder = getheadq(&psossemq);
	 holder; holder = nextq(&psossemq,holder))
	{
	sem = link2psossem(holder);

	if (sem->name[0] == name[0] &&
	    sem->name[1] == name[1] &&
	    sem->name[2] == name[2] &&
	    sem->name[3] == name[3])
	    {
	    *smid = (u_long)sem;
	    goto unlock_and_exit;
	    }
	}

    err = ERR_OBJNF;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

u_long sm_p (u_long smid,
	     u_long flags,
	     u_long timeout)
{
    u_long err = SUCCESS;
    psossem_t *sem;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    sem = psos_h2obj_active(smid,PSOS_SEM_MAGIC,psossem_t);

    if (!sem)
	{
	err = psos_handle_error(smid,PSOS_SEM_MAGIC,psossem_t);
	goto unlock_and_exit;
	}

    if (flags & SM_NOWAIT)
	{
	if (sem->count > 0)
	    sem->count--;
	else
	    err = ERR_NOSEM;
	}
    else
	{
	xnpod_check_context(XNPOD_THREAD_CONTEXT);

	if (sem->count > 0)
	    sem->count--;
	else
	    {
	    xnsynch_sleep_on(&sem->synchbase,timeout);

	    if (xnthread_test_flags(&psos_current_task()->threadbase,XNRMID))
		err = ERR_SKILLD; /* Semaphore deleted while pending. */
	    else if (xnthread_test_flags(&psos_current_task()->threadbase,XNTIMEO))
		err = ERR_TIMEOUT; /* Timeout.*/
	    }
	}

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

u_long sm_v (u_long smid)

{
    u_long err = SUCCESS;
    psossem_t *sem;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    sem = psos_h2obj_active(smid,PSOS_SEM_MAGIC,psossem_t);

    if (!sem)
	{
	err = psos_handle_error(smid,PSOS_SEM_MAGIC,psossem_t);
	goto unlock_and_exit;
	}

    if (xnsynch_wakeup_one_sleeper(&sem->synchbase) != NULL)
	xnpod_schedule();
    else
	sem->count++;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/*
 * IMPLEMENTATION NOTES:
 *
 * - Code executing on behalf of interrupt context is currently not
 * allowed to scan/alter the global sema4 queue (psossemq).
 */

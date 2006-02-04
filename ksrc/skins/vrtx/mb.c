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
#include "vrtx/mb.h"

static xnqueue_t vrtxmbq;

void vrtxmb_init (void)
{
    initq(&vrtxmbq);
}

void vrtxmb_cleanup (void)
{
    vrtxmsg_t *msg_slot;
    xnholder_t *holder;

    while ((holder = getq(&vrtxmbq)) != NULL)
	{
	msg_slot = (vrtxmsg_t *)holder;
	xnsynch_destroy(&msg_slot->synchbase);
	xnfree(msg_slot);
	}
}

char *sc_accept (char **mboxp, int *errp)
{
    char *msg;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    msg = *mboxp;

    if (msg == 0)
	{
	*errp = ER_NMP;
	}
    else
	{
	*mboxp = 0;
	*errp = RET_OK;
	}

    xnlock_put_irqrestore(&nklock,s);

    return msg;
}

/**
  Manages a hash of xnsynch_t objects, indexed by mailboxes addresses.
  Given a mailbox, returns its synch.
  If the synch is not found, creates one,
*/
xnsynch_t * mb_get_synch_internal(char **mboxp)
{
    xnholder_t *holder;
    vrtxmsg_t *msg_slot;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    for (holder = getheadq(&vrtxmbq);
	 holder != NULL; holder = nextq(&vrtxmbq, holder))
	{
	if ( ((vrtxmsg_t *)holder)->mboxp == mboxp)
	    {
	    xnlock_put_irqrestore(&nklock,s);
	    return &((vrtxmsg_t *)holder)->synchbase;
	    }
	}

    /* not found */
    msg_slot = (vrtxmsg_t *)xnmalloc(sizeof(*msg_slot));

    inith(&msg_slot->link);
    msg_slot->mboxp = mboxp;
    xnsynch_init(&msg_slot->synchbase ,XNSYNCH_PRIO);

    appendq(&vrtxmbq, &msg_slot->link);

    xnlock_put_irqrestore(&nklock,s);

    return &msg_slot->synchbase;
}

char *sc_pend (char **mboxp, long timeout, int *errp)
{
    char *msg;
    xnsynch_t *synchbase;
    vrtxtask_t *task;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    msg = *mboxp;

    if (msg == 0)
	{
	synchbase = mb_get_synch_internal(mboxp);
	
	task = vrtx_current_task();
	task->vrtxtcb.TCBSTAT = TBSMBOX;
	if (timeout)
	    task->vrtxtcb.TCBSTAT |= TBSDELAY;

	xnsynch_sleep_on(synchbase,timeout);

	if (xnthread_test_flags(&task->threadbase,XNTIMEO))
	    {
	    xnlock_put_irqrestore(&nklock,s);
	    *errp = ER_TMO;
	    return NULL; /* Timeout.*/
	    }
	msg = vrtx_current_task()->waitargs.qmsg;
	}
    else
	{
	*mboxp = 0;
	}

    xnlock_put_irqrestore(&nklock,s);

    *errp = RET_OK;

    return msg;
}

void sc_post (char **mboxp, char *msg, int *errp)
{
    xnsynch_t *synchbase;
    xnthread_t *waiter;
    spl_t s;

    if (msg == 0)
	{
	*errp = ER_ZMW;
	return;
	}

    if (*mboxp != 0)
	{
	*errp = ER_MIU;
	return;
	}

    *errp = RET_OK;

    xnlock_get_irqsave(&nklock,s);

    synchbase = mb_get_synch_internal(mboxp);

    /* xnsynch_wakeup_one_sleeper() readies the thread */
    waiter = xnsynch_wakeup_one_sleeper(synchbase);
    
    if (waiter)
	{
	thread2vrtxtask(waiter)->waitargs.qmsg = msg;
	xnpod_schedule();
	}
    else
	*mboxp = msg;

    xnlock_put_irqrestore(&nklock,s);
}

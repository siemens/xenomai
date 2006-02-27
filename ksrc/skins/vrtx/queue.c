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
#include "vrtx/queue.h"

static vrtxqueue_t *vrtxqueuemap[VRTX_MAX_QID];

static u_long q_destroy_internal(vrtxqueue_t *queue);

u_long q_destroy_internal(vrtxqueue_t *queue)
{
    u_long rv;
    xnholder_t *holder;
    rv = xnsynch_destroy(&queue->synchbase);

    while ((holder = getq(&queue->messageq)) != NULL)
	xnfree(link2vrtxmsg(holder));

    xnfree(queue);

    return rv;
}

void vrtxqueue_init (void)
{
    int qid;
    for (qid = 0 ; qid < VRTX_MAX_QID ; qid++)
	{
	vrtxqueuemap[qid] = NULL;
	}
}

void vrtxqueue_cleanup (void)
{
    int qid;

    for (qid = 0 ; qid < VRTX_MAX_QID ; qid++)
	{
	if (NULL != vrtxqueuemap[qid])
	    {
	    /* depending of the number of queues, and the cost of
	       CRITICAL_ operations, we may prefer protect from interrupts
	       here only */
	    q_destroy_internal(vrtxqueuemap[qid]);
	    }
	}
}

int sc_qecreate (int qid, int qsize, int opt, int *errp)
{
    vrtxqueue_t *queue;
    int bflags;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if ( ( (qid < -1) || (qid >= VRTX_MAX_QID) ) ||
	 ( (opt != 0) && (opt != 1) ) )
	{
	*errp = ER_IIP;
	return -1;
	}

    xnlock_get_irqsave(&nklock,s);

    if (qid == -1)
	{
	for (qid = 0; qid < VRTX_MAX_QID; qid++)
	    {
	    if (vrtxqueuemap[qid] == NULL)
		break;
	    }

	if (qid >= VRTX_MAX_QID)
	    {
	    xnlock_put_irqrestore(&nklock,s);
	    *errp = ER_MEM;
	    return -1;
	    }
	}
    else if (qid > 0 && vrtxqueuemap[qid] != NULL)
	{
	xnlock_put_irqrestore(&nklock,s);
	*errp = ER_QID;
	return -1;
	}

    vrtxqueuemap[qid] = (vrtxqueue_t *)1;	/* Reserve slot */

    xnlock_put_irqrestore(&nklock,s);

    queue = (vrtxqueue_t *)xnmalloc(sizeof(*queue));
    if (queue == NULL)
	{
	vrtxqueuemap[qid] = NULL;
	*errp = ER_MEM;
	return -1;
	}

    /* messages[0] is reserved, others are indexed from 1 to qsize.
    */
    initq(&queue->messageq);

    if (opt == 1)
	{
	bflags = XNSYNCH_FIFO;
	}
    else
	{
	bflags = XNSYNCH_PRIO;
	}

    xnsynch_init(&queue->synchbase, bflags|XNSYNCH_DREORD);

    queue->magic = VRTX_QUEUE_MAGIC;
    queue->maxnum = qsize;
    vrtxqueuemap[qid] = queue;
    *errp = RET_OK;
    return qid;
}

int sc_qcreate (int qid, int qsize, int *errp)
{
    return sc_qecreate(qid, qsize, 1, errp);
}

void sc_qdelete (int qid, int opt, int *errp)
{
    vrtxqueue_t *queue;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if ( (opt != 0) && (opt != 1) )
	{
	*errp = ER_IIP;
	return;
	}

    xnlock_get_irqsave(&nklock,s);

    queue = vrtxqueuemap[qid];

    if (queue == NULL)
	{
	xnlock_put_irqrestore(&nklock,s);
	*errp = ER_QID;
	return;
	}

    if (opt == 0 && xnsynch_nsleepers(&queue->synchbase) > 0) /* we look for pending task */
	{
	xnlock_put_irqrestore(&nklock,s);
	*errp = ER_PND;
	return;
	}

    *errp = RET_OK;

    /* forcing delete or no task pending */
    if (q_destroy_internal(queue) == XNSYNCH_RESCHED)
	{
	/* we do not want to have an unaffected qid for another task */
	vrtxqueuemap[qid] = NULL;
	xnpod_schedule();
	}
    else
	{
	vrtxqueuemap[qid] = NULL;
	}

    xnlock_put_irqrestore(&nklock,s);

    *errp = RET_OK;
}

static void sc_qpost_internal (int qid, char *msg, int *errp, int to_head)
{
    vrtxqueue_t *queue;
    vrtxqmsg_t *msg_slot;
    xnthread_t *waiter;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    queue = vrtxqueuemap[qid];

    if (queue == NULL)
	{
	xnlock_put_irqrestore(&nklock,s);
	*errp = ER_QID;
	return;
	}

    *errp = RET_OK;
    waiter = xnsynch_wakeup_one_sleeper(&queue->synchbase);
    
    if (waiter)
	{
	thread2vrtxtask(waiter)->waitargs.qmsg = msg;
	xnpod_schedule();
	xnlock_put_irqrestore(&nklock,s);
	return;
	}

/*     count = countq(&queue->messageq); */
/*     if ( ( (to_head == 0) && (count >= queue->maxnum)) || */
/*  	    ( (to_head == 1) && (count > queue->maxnum))) */
/* optimized below : */
    if ( countq(&queue->messageq) >= queue->maxnum + to_head )
	{
	xnlock_put_irqrestore(&nklock,s);
	*errp = ER_QFL;
	return;
	}

    msg_slot = (vrtxqmsg_t *)xnmalloc(sizeof(*msg_slot));

    if (msg_slot == NULL)
	{
	xnlock_put_irqrestore(&nklock,s);
	*errp = ER_QFL;
	return;
	}

    msg_slot->message = msg;
    inith(&msg_slot->link);
    if (to_head == 0)
	{
	appendq(&queue->messageq, &msg_slot->link);
	}
    else
	{
	prependq(&queue->messageq, &msg_slot->link);
	}	

    xnlock_put_irqrestore(&nklock,s);
}

void sc_qpost (int qid, char *msg, int *errp)
{
    sc_qpost_internal(qid, msg, errp, 0);
}

void sc_qjam (int qid, char *msg, int *errp)
{
    sc_qpost_internal(qid, msg, errp, 1);
}

char *sc_qpend (int qid, long timeout, int *errp)
{
    vrtxqueue_t *queue;
    xnholder_t *holder;
    vrtxtask_t *task;
    vrtxqmsg_t *qmsg;
    char *msg;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnlock_get_irqsave(&nklock,s);

    queue = vrtxqueuemap[qid];

    if (queue == NULL)
	{
	xnlock_put_irqrestore(&nklock,s);
	*errp = ER_QID;
	return NULL;
	}

    holder = getq(&queue->messageq);

    if (holder == NULL)
	{
	task = vrtx_current_task();    
	task->vrtxtcb.TCBSTAT = TBSQUEUE;
	if (timeout)
	    {
	    task->vrtxtcb.TCBSTAT |= TBSDELAY;
	    }

	xnsynch_sleep_on(&queue->synchbase,timeout);

	if (xnthread_test_flags(&task->threadbase, XNRMID))
	    {
	    xnlock_put_irqrestore(&nklock,s);
	    *errp = ER_DEL;
	    return NULL; /* Queue deleted while pending. */
	    }
	
	if (xnthread_test_flags(&task->threadbase, XNTIMEO))
	    {
	    xnlock_put_irqrestore(&nklock,s);
	    *errp = ER_TMO;
	    return NULL; /* Timeout.*/
	    }
	msg = vrtx_current_task()->waitargs.qmsg;
	}
    else
	{
	qmsg = link2vrtxmsg(holder);
	msg = qmsg->message;
	xnfree(qmsg);
	}

    xnlock_put_irqrestore(&nklock,s);

    *errp = RET_OK;

    return msg;
}

char *sc_qaccept(int qid, int *errp)
{
    vrtxqueue_t *queue;
    xnholder_t *holder;
    char *msg;
    vrtxqmsg_t *qmsg;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    queue = vrtxqueuemap[qid];

    if (queue == NULL)
	{
	xnlock_put_irqrestore(&nklock,s);
	*errp = ER_QID;
	return NULL;
	}

    holder = getq(&queue->messageq);
    if (holder == NULL)
	{
	xnlock_put_irqrestore(&nklock,s);
	*errp = ER_NMP;
	return NULL;
	}

    qmsg = link2vrtxmsg(holder);
    msg = qmsg->message;
    xnfree(qmsg);

    xnlock_put_irqrestore(&nklock,s);

    *errp = RET_OK;

    return msg;
}

void sc_qbrdcst(int qid, char *msg, int *errp)
{
    xnthread_t *waiter;
    vrtxqueue_t *queue;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    queue = vrtxqueuemap[qid];

    if (queue == NULL)
	{
	xnlock_put_irqrestore(&nklock,s);
	*errp = ER_QID;
	return;
	}

    while ((waiter = xnsynch_wakeup_one_sleeper(&queue->synchbase)) != NULL)
	thread2vrtxtask(waiter)->waitargs.qmsg = msg;

    xnpod_schedule();

    xnlock_put_irqrestore(&nklock,s);

    *errp = RET_OK;
}

char *sc_qinquiry (int qid, int *countp, int *errp)
{
    vrtxqueue_t *queue;
    char *msg = NULL;
    xnholder_t *holder;
    spl_t s;

    *countp = 0;

    xnlock_get_irqsave(&nklock,s);

    queue = vrtxqueuemap[qid];

    if (queue == NULL)
	{
	xnlock_put_irqrestore(&nklock,s);
	*errp = ER_QID;
	return NULL;
	}
    
    holder = getheadq(&queue->messageq);
    if (holder != NULL)
	{
	*countp = countq(&queue->messageq);
	msg = link2vrtxmsg(holder)->message;
	}

    xnlock_put_irqrestore(&nklock,s);

    *errp = RET_OK;
    
    return msg;
}

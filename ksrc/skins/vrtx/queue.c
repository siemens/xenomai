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
#include <vrtx/queue.h>

static vrtxidmap_t *vrtx_queue_idmap;

static xnqueue_t vrtx_queue_q;

int queue_destroy_internal(vrtxqueue_t *queue)
{
    int s;

    removeq(&vrtx_queue_q,&queue->link);
    s = xnsynch_destroy(&queue->synchbase);
    vrtx_put_id(vrtx_queue_idmap,queue->qid);
    xnfree(queue->messages);
    xnfree(queue);

    return s;
}

int vrtxqueue_init (void)
{
    initq(&vrtx_queue_q);
    vrtx_queue_idmap = vrtx_alloc_idmap(VRTX_MAX_QUEUES,1);
    return vrtx_queue_idmap ? 0 : -ENOMEM;
}

void vrtxqueue_cleanup (void)
{
    xnholder_t *holder;

    while ((holder = getheadq(&vrtx_queue_q)) != NULL)
	queue_destroy_internal(link2vrtxqueue(holder));

    vrtx_free_idmap(vrtx_queue_idmap);
}

int sc_qecreate (int qid, int qsize, int opt, int *errp)
{
    vrtxqueue_t *queue;
    int bflags;
    spl_t s;

    if ((opt & ~1) || qid < -1 || qsize < 0 || qsize > 65535)
	{
	*errp = ER_IIP;
	return -1;
	}

    queue = (vrtxqueue_t *)xnmalloc(sizeof(*queue));

    if (queue == NULL)
	goto nomem;

    /* Allocate enough message entries, +1 for the jamming slot. */
    queue->messages = (char **)xnmalloc(sizeof(char *) * (qsize + 1));

    if (queue->messages == NULL)
	{
	xnfree(queue);
 nomem:
	*errp = ER_MEM;
	return -1;
	}

    qid = vrtx_get_id(vrtx_queue_idmap,qid,queue);

    if (qid < 0)
	{
	xnfree(queue->messages);
	xnfree(queue);
	*errp = ER_QID;
	return -1;
	}

    if (opt == 1)
	bflags = XNSYNCH_FIFO;
    else
	bflags = XNSYNCH_PRIO;

    inith(&queue->link);
    xnsynch_init(&queue->synchbase, bflags|XNSYNCH_DREORD);
    queue->magic = VRTX_QUEUE_MAGIC;
    queue->qid = qid;
    queue->qsize = qsize;
    queue->rdptr = 0;
    queue->wrptr = 0;
    queue->qused = 0;

    *errp = RET_OK;

    xnlock_get_irqsave(&nklock,s);
    appendq(&vrtx_queue_q,&queue->link);
    xnlock_put_irqrestore(&nklock,s);

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

    if (opt & ~1)
	{
	*errp = ER_IIP;
	return;
	}

    xnlock_get_irqsave(&nklock,s);

    queue = (vrtxqueue_t *)vrtx_get_object(vrtx_queue_idmap,qid);

    if (queue == NULL)
	{
	*errp = ER_QID;
	goto unlock_and_exit;
	}

    /* We look for a pending task */
    if (opt == 0 && xnsynch_nsleepers(&queue->synchbase) > 0)
	{
	*errp = ER_PND;
	goto unlock_and_exit;
	}

    *errp = RET_OK;

    /* forcing delete or no task pending */
    if (queue_destroy_internal(queue) == XNSYNCH_RESCHED)
	xnpod_schedule();

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);
}

static void sc_qpost_inner (int qid, char *msg, int *errp, int jammed)
{
    vrtxqueue_t *queue;
    xnthread_t *waiter;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    queue = (vrtxqueue_t *)vrtx_get_object(vrtx_queue_idmap,qid);

    if (queue == NULL)
	{
	*errp = ER_QID;
	goto unlock_and_exit;
	}

    *errp = RET_OK;

    waiter = xnsynch_wakeup_one_sleeper(&queue->synchbase);
    
    if (waiter)
	{
	thread2vrtxtask(waiter)->waitargs.msg = msg;
	xnpod_schedule();
	goto unlock_and_exit;
	}

    if (queue->qused >= queue->qsize + jammed)
	{
	*errp = ER_QFL;
	goto unlock_and_exit;
	}

    if (jammed)
	{
	queue->rdptr = queue->rdptr == 0 ? queue->qsize : queue->rdptr - 1;
	queue->messages[queue->rdptr] = msg;
	}
    else
	{
	queue->messages[queue->wrptr] = msg;
	queue->wrptr = (queue->wrptr + 1) % (queue->qsize + 1);
	}

    ++queue->qused;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);
}

void sc_qpost (int qid, char *msg, int *errp)
{
    sc_qpost_inner(qid, msg, errp, 0);
}

void sc_qjam (int qid, char *msg, int *errp)
{
    sc_qpost_inner(qid, msg, errp, 1);
}

char *sc_qpend (int qid, long timeout, int *errp)
{
    vrtxqueue_t *queue;
    vrtxtask_t *task;
    char *msg = NULL;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    queue = (vrtxqueue_t *)vrtx_get_object(vrtx_queue_idmap,qid);

    if (queue == NULL)
	{
	*errp = ER_QID;
	goto unlock_and_exit;
	}

    if (likely(queue->qused > 0))
	{
	msg = queue->messages[queue->rdptr];
	queue->rdptr = (queue->rdptr + 1) % (queue->qsize + 1);
	--queue->qused;
	goto unlock_and_exit;
	}

    if (xnpod_unblockable_p())
	{
	*errp = -EPERM;
	goto unlock_and_exit;
	}

    task = vrtx_current_task();    
    task->vrtxtcb.TCBSTAT = TBSQUEUE;

    if (timeout)
	task->vrtxtcb.TCBSTAT |= TBSDELAY;

    xnsynch_sleep_on(&queue->synchbase,timeout);

    if (xnthread_test_flags(&task->threadbase, XNBREAK))
	{
	*errp = -EINTR;
	goto unlock_and_exit;
	}

    if (xnthread_test_flags(&task->threadbase, XNRMID))
	{
	*errp = ER_DEL;
	goto unlock_and_exit;
	}
	
    if (xnthread_test_flags(&task->threadbase, XNTIMEO))
	{
	*errp = ER_TMO;
	goto unlock_and_exit;
	}

    msg = task->waitargs.msg;

    *errp = RET_OK;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return msg;
}

char *sc_qaccept(int qid, int *errp)
{
    vrtxqueue_t *queue;
    char *msg = NULL;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    queue = (vrtxqueue_t *)vrtx_get_object(vrtx_queue_idmap,qid);

    if (queue == NULL)
	{
	*errp = ER_QID;
	goto unlock_and_exit;
	}

    if (queue->qused == 0)
	{
	*errp = ER_NMP;
	goto unlock_and_exit;
	}

    msg = queue->messages[queue->rdptr];
    queue->rdptr = (queue->rdptr + 1) % (queue->qsize + 1);
    --queue->qused;

    *errp = RET_OK;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return msg;
}

void sc_qbrdcst(int qid, char *msg, int *errp)
{
    xnthread_t *waiter;
    vrtxqueue_t *queue;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    queue = (vrtxqueue_t *)vrtx_get_object(vrtx_queue_idmap,qid);

    if (queue == NULL)
	{
	*errp = ER_QID;
	goto unlock_and_exit;
	}

    while ((waiter = xnsynch_wakeup_one_sleeper(&queue->synchbase)) != NULL)
	thread2vrtxtask(waiter)->waitargs.msg = msg;

    *errp = RET_OK;

    xnpod_schedule();

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);
}

char *sc_qinquiry (int qid, int *countp, int *errp)
{
    vrtxqueue_t *queue;
    char *msg;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    queue = (vrtxqueue_t *)vrtx_get_object(vrtx_queue_idmap,qid);

    if (queue == NULL)
	{
	msg = NULL;
	*errp = ER_QID;
	goto unlock_and_exit;
	}
   
    *countp = queue->qused;
    msg = queue->messages[queue->rdptr];
    *errp = RET_OK;

 unlock_and_exit:
    
    xnlock_put_irqrestore(&nklock,s);

    return msg;
}

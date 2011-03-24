/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
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

#include <vxworks/defs.h>

static int msgq_destroy_internal(wind_msgq_t *queue);

#ifdef CONFIG_XENO_OPT_VFILE

struct vfile_priv {
	struct xnpholder *curr;
	int flags;
	unsigned int mlength;
	int mcount;
};

struct vfile_data {
	char name[XNOBJECT_NAME_LEN];
};

static int vfile_rewind(struct xnvfile_snapshot_iterator *it)
{
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	wind_msgq_t *q = xnvfile_priv(it->vfile);

	q = wind_h2obj_active((MSG_Q_ID)q, WIND_MSGQ_MAGIC, wind_msgq_t);
	if (q == NULL)
		return -EIDRM;

	priv->curr = getheadpq(xnsynch_wait_queue(&q->synchbase));
	priv->flags = xnsynch_test_flags(&q->synchbase, XNSYNCH_PRIO);
	priv->mlength = q->msg_length;
	priv->mcount = countq(&q->msgq);

	return xnsynch_nsleepers(&q->synchbase);
}

static int vfile_next(struct xnvfile_snapshot_iterator *it, void *data)
{
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	wind_msgq_t *q = xnvfile_priv(it->vfile);
	struct vfile_data *p = data;
	struct xnthread *thread;

	if (priv->curr == NULL)
		return 0;	/* We are done. */

	/* Fetch current waiter, advance list cursor. */
	thread = link2thread(priv->curr, plink);
	priv->curr = nextpq(xnsynch_wait_queue(&q->synchbase),
			    priv->curr);
	/* Collect thread name to be output in ->show(). */
	strncpy(p->name, xnthread_name(thread), sizeof(p->name));

	return 1;
}

static int vfile_show(struct xnvfile_snapshot_iterator *it, void *data)
{
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	struct vfile_data *p = data;

	if (p == NULL) {	/* Dump header. */
		xnvfile_printf(it, 
			       "porder=%s:mlength=%u:mcount=%d\n",
			       priv->flags ? "prio" : "fifo",
			       priv->mlength,
			       priv->mcount);
		if (it->nrdata > 0)
			/* Queue is pended -- dump waiters */
			xnvfile_printf(it, "-------------------------------------------\n");
	} else
		xnvfile_printf(it, "%.*s\n",
			       (int)sizeof(p->name), p->name);

	return 0;
}

static struct xnvfile_snapshot_ops vfile_ops = {
	.rewind = vfile_rewind,
	.next = vfile_next,
	.show = vfile_show,
};

extern struct xnptree __vxworks_ptree;

static struct xnpnode_snapshot __msgq_pnode = {
	.node = {
		.dirname = "msgq",
		.root = &__vxworks_ptree,
		.ops = &xnregistry_vfsnap_ops,
	},
	.vfile = {
		.privsz = sizeof(struct vfile_priv),
		.datasz = sizeof(struct vfile_data),
		.ops = &vfile_ops,
	},
};

#else /* !CONFIG_XENO_OPT_VFILE */

static struct xnpnode_snapshot __msgq_pnode = {
	.node = {
		.dirname = "msgq",
	},
};

#endif /* !CONFIG_XENO_OPT_VFILE */

void wind_msgq_init(void)
{
}

void wind_msgq_cleanup(void)
{
	wind_msgq_flush_rq(&__wind_global_rholder.msgQq);
}

/* free_msg: return a message to the free list */
static inline void free_msg(wind_msgq_t *queue, wind_msg_t *msg)
{
	msg->link.next = queue->free_list;
	queue->free_list = &msg->link;
}

/* get a message from the free list */
static inline wind_msg_t *get_free_msg(wind_msgq_t *queue)
{
	wind_msg_t *msg;

	if (queue->free_list == NULL)
		return NULL;

	msg = link2wind_msg(queue->free_list);
	queue->free_list = queue->free_list->next;
	inith(&msg->link);

	return msg;
}

/* try to unqueue message for reading */
static inline wind_msg_t *unqueue_msg(wind_msgq_t *queue)
{
	xnholder_t *holder;
	wind_msg_t *msg;

	holder = getheadq(&queue->msgq);
	if (holder == NULL)
		return NULL;

	msg = link2wind_msg(holder);
	removeq(&queue->msgq, holder);

	return msg;
}

MSG_Q_ID msgQCreate(int nb_msgs, int length, int flags)
{
	static unsigned long msgq_ids;
	wind_msgq_t *queue;
	xnflags_t bflags = 0;
	int i, msg_size;
	char *msgs_mem;
	spl_t s;

	check_NOT_ISR_CALLABLE(return 0);

	error_check(nb_msgs <= 0, S_msgQLib_INVALID_QUEUE_TYPE, return 0);

	error_check(flags & ~WIND_MSG_Q_OPTION_MASK,
		    S_msgQLib_INVALID_QUEUE_TYPE, return 0);

	error_check(length < 0, S_msgQLib_INVALID_MSG_LENGTH, return 0);

	msgs_mem = xnmalloc(sizeof(wind_msgq_t) +
			    nb_msgs * (sizeof(wind_msg_t) + length));

	error_check(msgs_mem == NULL, S_memLib_NOT_ENOUGH_MEMORY, return 0);

	queue = (wind_msgq_t *)msgs_mem;
	msgs_mem += sizeof(wind_msgq_t);

	queue->magic = WIND_MSGQ_MAGIC;
	queue->msg_length = length;
	queue->free_list = NULL;
	initq(&queue->msgq);
	inith(&queue->rlink);
	queue->rqueue = &wind_get_rholder()->msgQq;

	/* init of the synch object : */
	if (flags & MSG_Q_PRIORITY)
		bflags |= XNSYNCH_PRIO;

	xnsynch_init(&queue->synchbase, bflags, NULL);

	msg_size = sizeof(wind_msg_t) + length;

	for (i = 0; i < nb_msgs; ++i, msgs_mem += msg_size)
		free_msg(queue, (wind_msg_t *)msgs_mem);

	xnlock_get_irqsave(&nklock, s);
	appendq(queue->rqueue, &queue->rlink);
	xnlock_put_irqrestore(&nklock, s);

	sprintf(queue->name, "mq%lu", msgq_ids++);

	if (xnregistry_enter(queue->name, queue,
			     &queue->handle, &__msgq_pnode.node)) {
		wind_errnoset(S_objLib_OBJ_ID_ERROR);
		msgQDelete((MSG_Q_ID)queue);
		return 0;
	}

	return (MSG_Q_ID)queue;
}

STATUS msgQDelete(MSG_Q_ID qid)
{
	wind_msgq_t *queue;
	spl_t s;

	check_NOT_ISR_CALLABLE(return ERROR);

	xnlock_get_irqsave(&nklock, s);

	check_OBJ_ID_ERROR(qid, wind_msgq_t, queue, WIND_MSGQ_MAGIC,
			   goto error);
	if (msgq_destroy_internal(queue) == XNSYNCH_RESCHED)
		xnpod_schedule();

	xnlock_put_irqrestore(&nklock, s);
	return OK;

      error:
	xnlock_put_irqrestore(&nklock, s);
	return ERROR;
}

int msgQNumMsgs(MSG_Q_ID qid)
{

	wind_msgq_t *queue;
	int result;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	check_OBJ_ID_ERROR(qid, wind_msgq_t, queue, WIND_MSGQ_MAGIC,
			   goto error);

	result = queue->msgq.elems;

	xnlock_put_irqrestore(&nklock, s);
	return result;

      error:
	xnlock_put_irqrestore(&nklock, s);
	return ERROR;
}

int msgQReceive(MSG_Q_ID qid, char *buf, UINT bytes, int to)
{
	xnticks_t timeout;
	wind_msgq_t *queue;
	wind_msg_t *msg;
	xnthread_t *thread;
	wind_task_t *task;
	spl_t s;

	error_check(buf == NULL, 0, return ERROR);

	check_NOT_ISR_CALLABLE(return ERROR);

	xnlock_get_irqsave(&nklock, s);

	check_OBJ_ID_ERROR(qid, wind_msgq_t, queue, WIND_MSGQ_MAGIC,
			   goto error);

	if ((msg = unqueue_msg(queue)) == NULL) {
		/* message queue is empty */

		error_check(to == NO_WAIT ||
			    xnpod_unblockable_p(), S_objLib_OBJ_UNAVAILABLE,
			    goto error);

		if (to == WAIT_FOREVER)
			timeout = XN_INFINITE;
		else
			timeout = to;

		task = wind_current_task();
		thread = &task->threadbase;
		task->rcv_buf = buf;
		task->rcv_bytes = bytes;

		xnsynch_sleep_on(&queue->synchbase, timeout, XN_RELATIVE);

		error_check(xnthread_test_info(thread, XNBREAK), -EINTR,
			    goto error);
		error_check(xnthread_test_info(thread, XNRMID),
			    S_objLib_OBJ_DELETED, goto error);
		error_check(xnthread_test_info(thread, XNTIMEO),
			    S_objLib_OBJ_TIMEOUT, goto error);

		bytes = task->rcv_bytes;
	} else {
		if (msg->length < bytes)
			bytes = msg->length;
		memcpy(buf, msg->buffer, bytes);
		free_msg(queue, msg);

		/* check if some sender is pending */
		if (xnsynch_wakeup_one_sleeper(&queue->synchbase))
			xnpod_schedule();
	}

	xnlock_put_irqrestore(&nklock, s);
	return bytes;

      error:
	xnlock_put_irqrestore(&nklock, s);
	return ERROR;
}

STATUS msgQSend(MSG_Q_ID qid, const char *buf, UINT bytes, int to, int prio)
{
	wind_msgq_t *queue;
	xnticks_t timeout;
	wind_msg_t *msg;
	xnthread_t *thread;
	wind_task_t *task;
	spl_t s;

	if (xnpod_asynch_p() && to != NO_WAIT) {
		wind_errnoset(S_msgQLib_NON_ZERO_TIMEOUT_AT_INT_LEVEL);
		return ERROR;
	}

	xnlock_get_irqsave(&nklock, s);

	check_OBJ_ID_ERROR(qid, wind_msgq_t, queue, WIND_MSGQ_MAGIC,
			   goto error);

	error_check(buf == NULL || bytes > queue->msg_length,
		    S_msgQLib_INVALID_MSG_LENGTH, goto error);

	if (queue->msgq.elems == 0 &&
	    (thread = xnsynch_wakeup_one_sleeper(&queue->synchbase)) != NULL) {
		/* the message queue is empty and we have found a pending receiver */
		task = thread2wind_task(thread);
		if (bytes < task->rcv_bytes)
			task->rcv_bytes = bytes;

		memcpy(task->rcv_buf, buf, bytes);
		xnpod_schedule();

	} else {
		msg = get_free_msg(queue);
		if (msg == NULL) {
			/* the message queue is full, we need to wait */
			error_check(to == NO_WAIT, S_objLib_OBJ_UNAVAILABLE,
				    goto error);

			thread = &wind_current_task()->threadbase;

			if (to == WAIT_FOREVER)
				timeout = XN_INFINITE;
			else
				timeout = to;

			xnsynch_sleep_on(&queue->synchbase, timeout, XN_RELATIVE);

			error_check(xnthread_test_info(thread, XNBREAK),
				    -EINTR, goto error);
			error_check(xnthread_test_info(thread, XNRMID),
				    S_objLib_OBJ_DELETED, goto error);
			error_check(xnthread_test_info(thread, XNTIMEO),
				    S_objLib_OBJ_TIMEOUT, goto error);

			/* a receiver unblocked us, so we are sure to obtain a message
			   buffer */
			msg = get_free_msg(queue);
		}

		msg->length = bytes;
		memcpy(msg->buffer, buf, bytes);
		if (prio == MSG_PRI_NORMAL)
			appendq(&queue->msgq, &msg->link);
		else		/* Anything else will be interpreted as MSG_PRI_URGENT. */
			prependq(&queue->msgq, &msg->link);
	}

	xnlock_put_irqrestore(&nklock, s);
	return OK;

      error:
	xnlock_put_irqrestore(&nklock, s);
	return ERROR;

}

static int msgq_destroy_internal(wind_msgq_t *queue)
{
	int s = xnsynch_destroy(&queue->synchbase);
	xnregistry_remove(queue->handle);
	wind_mark_deleted(queue);
	removeq(queue->rqueue, &queue->rlink);
	xnfree(queue);
	return s;
}

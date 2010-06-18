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

static xnmap_t *vrtx_queue_idmap;

static xnqueue_t vrtx_queue_q;

#ifdef CONFIG_XENO_OPT_VFILE

struct vfile_priv {
	struct xnpholder *curr;
	int qused;
	int qsize;
};

struct vfile_data {
	char name[XNOBJECT_NAME_LEN];
};

static int vfile_rewind(struct xnvfile_snapshot_iterator *it)
{
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	struct vrtxqueue *queue = xnvfile_priv(it->vfile);

	priv->curr = getheadpq(xnsynch_wait_queue(&queue->synchbase));
	priv->qused = queue->qused;
	priv->qsize = queue->qsize;

	return xnsynch_nsleepers(&queue->synchbase);
}

static int vfile_next(struct xnvfile_snapshot_iterator *it, void *data)
{
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	struct vrtxqueue *queue = xnvfile_priv(it->vfile);
	struct vfile_data *p = data;
	struct xnthread *thread;

	if (priv->curr == NULL)
		return 0;	/* We are done. */

	/* Fetch current waiter, advance list cursor. */
	thread = link2thread(priv->curr, plink);
	priv->curr = nextpq(xnsynch_wait_queue(&queue->synchbase),
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
		xnvfile_printf(it, "mcount=%d, qsize=%d\n",
			       priv->qused, priv->qsize);
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

extern struct xnptree __vrtx_ptree;

static struct xnpnode_snapshot __queue_pnode = {
	.node = {
		.dirname = "queues",
		.root = &__vrtx_ptree,
		.ops = &xnregistry_vfsnap_ops,
	},
	.vfile = {
		.privsz = sizeof(struct vfile_priv),
		.datasz = sizeof(struct vfile_data),
		.ops = &vfile_ops,
	},
};

#else /* !CONFIG_XENO_OPT_VFILE */

static struct xnpnode_snapshot __queue_pnode = {
	.node = {
		.dirname = "queues",
	},
};

#endif /* !CONFIG_XENO_OPT_VFILE */

int queue_destroy_internal(vrtxqueue_t * queue)
{
	int s;

	removeq(&vrtx_queue_q, &queue->link);
	s = xnsynch_destroy(&queue->synchbase);
	xnmap_remove(vrtx_queue_idmap, queue->qid);
	xnfree(queue->messages);
	xnregistry_remove(queue->handle);
	vrtx_mark_deleted(queue);
	xnfree(queue);

	return s;
}

int vrtxqueue_init(void)
{
	initq(&vrtx_queue_q);
	vrtx_queue_idmap = xnmap_create(VRTX_MAX_QUEUES, VRTX_MAX_QUEUES / 2, 0);
	return vrtx_queue_idmap ? 0 : -ENOMEM;
}

void vrtxqueue_cleanup(void)
{
	xnholder_t *holder;

	while ((holder = getheadq(&vrtx_queue_q)) != NULL)
		queue_destroy_internal(link2vrtxqueue(holder));

	xnmap_delete(vrtx_queue_idmap);
}

int sc_qecreate(int qid, int qsize, int opt, int *errp)
{
	vrtxqueue_t *queue;
	int bflags;
	spl_t s;

	if ((opt & ~1) || qid < -1 || qsize < 0 || qsize > 65535) {
		*errp = ER_IIP;
		return -1;
	}

	queue = (vrtxqueue_t *) xnmalloc(sizeof(*queue));

	if (queue == NULL)
		goto nomem;

	/* Allocate enough message entries, +1 for the jamming slot. */
	queue->messages = (char **)xnmalloc(sizeof(char *) * (qsize + 1));

	if (queue->messages == NULL) {
		xnfree(queue);
	      nomem:
		*errp = ER_MEM;
		return -1;
	}

	qid = xnmap_enter(vrtx_queue_idmap, qid, queue);

	if (qid < 0) {
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
	xnsynch_init(&queue->synchbase, bflags | XNSYNCH_DREORD, NULL);
	queue->magic = VRTX_QUEUE_MAGIC;
	queue->qid = qid;
	queue->qsize = qsize;
	queue->rdptr = 0;
	queue->wrptr = 0;
	queue->qused = 0;

	*errp = RET_OK;

	xnlock_get_irqsave(&nklock, s);
	appendq(&vrtx_queue_q, &queue->link);
	xnlock_put_irqrestore(&nklock, s);

	sprintf(queue->name, "q%d", qid);
	xnregistry_enter(queue->name, queue, &queue->handle, &__queue_pnode.node);

	return qid;
}

int sc_qcreate(int qid, int qsize, int *errp)
{
	return sc_qecreate(qid, qsize, 1, errp);
}

void sc_qdelete(int qid, int opt, int *errp)
{
	vrtxqueue_t *queue;
	spl_t s;

	if (opt & ~1) {
		*errp = ER_IIP;
		return;
	}

	xnlock_get_irqsave(&nklock, s);

	queue = xnmap_fetch(vrtx_queue_idmap, qid);

	if (queue == NULL) {
		*errp = ER_QID;
		goto unlock_and_exit;
	}

	/* We look for a pending task */
	if (opt == 0 && xnsynch_nsleepers(&queue->synchbase) > 0) {
		*errp = ER_PND;
		goto unlock_and_exit;
	}

	*errp = RET_OK;

	/* forcing delete or no task pending */
	if (queue_destroy_internal(queue) == XNSYNCH_RESCHED)
		xnpod_schedule();

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);
}

static void sc_qpost_inner(int qid, char *msg, int *errp, int jammed)
{
	vrtxqueue_t *queue;
	xnthread_t *waiter;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	queue = xnmap_fetch(vrtx_queue_idmap, qid);

	if (queue == NULL) {
		*errp = ER_QID;
		goto unlock_and_exit;
	}

	*errp = RET_OK;

	waiter = xnsynch_wakeup_one_sleeper(&queue->synchbase);

	if (waiter) {
		thread2vrtxtask(waiter)->waitargs.msg = msg;
		xnpod_schedule();
		goto unlock_and_exit;
	}

	if (queue->qused >= queue->qsize + jammed) {
		*errp = ER_QFL;
		goto unlock_and_exit;
	}

	if (jammed) {
		queue->rdptr =
		    queue->rdptr == 0 ? queue->qsize : queue->rdptr - 1;
		queue->messages[queue->rdptr] = msg;
	} else {
		queue->messages[queue->wrptr] = msg;
		queue->wrptr = (queue->wrptr + 1) % (queue->qsize + 1);
	}

	++queue->qused;

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);
}

void sc_qpost(int qid, char *msg, int *errp)
{
	sc_qpost_inner(qid, msg, errp, 0);
}

void sc_qjam(int qid, char *msg, int *errp)
{
	sc_qpost_inner(qid, msg, errp, 1);
}

char *sc_qpend(int qid, long timeout, int *errp)
{
	vrtxqueue_t *queue;
	vrtxtask_t *task;
	char *msg = NULL;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	queue = xnmap_fetch(vrtx_queue_idmap, qid);

	if (queue == NULL) {
		*errp = ER_QID;
		goto unlock_and_exit;
	}

	if (likely(queue->qused > 0)) {
		msg = queue->messages[queue->rdptr];
		queue->rdptr = (queue->rdptr + 1) % (queue->qsize + 1);
		--queue->qused;
		goto unlock_and_exit;
	}

	if (xnpod_unblockable_p()) {
		*errp = -EPERM;
		goto unlock_and_exit;
	}

	task = vrtx_current_task();
	task->vrtxtcb.TCBSTAT = TBSQUEUE;

	if (timeout)
		task->vrtxtcb.TCBSTAT |= TBSDELAY;

	xnsynch_sleep_on(&queue->synchbase, timeout, XN_RELATIVE);

	if (xnthread_test_info(&task->threadbase, XNBREAK)) {
		*errp = -EINTR;
		goto unlock_and_exit;
	}

	if (xnthread_test_info(&task->threadbase, XNRMID)) {
		*errp = ER_DEL;
		goto unlock_and_exit;
	}

	if (xnthread_test_info(&task->threadbase, XNTIMEO)) {
		*errp = ER_TMO;
		goto unlock_and_exit;
	}

	msg = task->waitargs.msg;

	*errp = RET_OK;

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return msg;
}

char *sc_qaccept(int qid, int *errp)
{
	vrtxqueue_t *queue;
	char *msg = NULL;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	queue = xnmap_fetch(vrtx_queue_idmap, qid);

	if (queue == NULL) {
		*errp = ER_QID;
		goto unlock_and_exit;
	}

	if (queue->qused == 0) {
		*errp = ER_NMP;
		goto unlock_and_exit;
	}

	msg = queue->messages[queue->rdptr];
	queue->rdptr = (queue->rdptr + 1) % (queue->qsize + 1);
	--queue->qused;

	*errp = RET_OK;

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return msg;
}

void sc_qbrdcst(int qid, char *msg, int *errp)
{
	xnthread_t *waiter;
	vrtxqueue_t *queue;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	queue = xnmap_fetch(vrtx_queue_idmap, qid);

	if (queue == NULL) {
		*errp = ER_QID;
		goto unlock_and_exit;
	}

	while ((waiter = xnsynch_wakeup_one_sleeper(&queue->synchbase)) != NULL)
		thread2vrtxtask(waiter)->waitargs.msg = msg;

	*errp = RET_OK;

	xnpod_schedule();

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);
}

char *sc_qinquiry(int qid, int *countp, int *errp)
{
	vrtxqueue_t *queue;
	char *msg;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	queue = xnmap_fetch(vrtx_queue_idmap, qid);

	if (queue == NULL) {
		msg = NULL;
		*errp = ER_QID;
		goto unlock_and_exit;
	}

	*countp = queue->qused;
	msg = queue->messages[queue->rdptr];
	*errp = RET_OK;

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return msg;
}

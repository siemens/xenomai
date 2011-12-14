/*
 * Copyright (C) 2011 Philippe Gerum <rpm@xenomai.org>.
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

#include <errno.h>
#include <string.h>
#include <copperplate/threadobj.h>
#include <copperplate/heapobj.h>
#include "reference.h"
#include "internal.h"
#include "queue.h"
#include "timer.h"

struct syncluster alchemy_queue_table;

static struct alchemy_namegen queue_namegen = {
	.prefix = "queue",
	.length = sizeof ((struct alchemy_queue *)0)->name,
};

DEFINE_SYNC_LOOKUP(queue, RT_QUEUE);

static void queue_finalize(struct syncobj *sobj)
{
	struct alchemy_queue *qcb;

	qcb = container_of(sobj, struct alchemy_queue, sobj);
	heapobj_destroy(&qcb->hobj);
	xnfree(qcb);
}
fnref_register(libalchemy, queue_finalize);

int rt_queue_create(RT_QUEUE *queue, const char *name,
		    size_t poolsize, size_t qlimit, int mode)
{
	struct alchemy_queue *qcb;
	int sobj_flags = 0, ret;
	struct service svc;

	if (threadobj_irq_p())
		return -EPERM;

	if (poolsize == 0 || (mode & ~Q_PRIO) != 0)
		return -EINVAL;

	COPPERPLATE_PROTECT(svc);

	ret = -ENOMEM;
	qcb = xnmalloc(sizeof(*qcb));
	if (qcb == NULL)
		goto out;

	alchemy_build_name(qcb->name, name, &queue_namegen);
	/*
	 * The message pool has to be part of the main heap for proper
	 * sharing between processes.
	 */
	if (qlimit == Q_UNLIMITED)
		ret = heapobj_init_shareable(&qcb->hobj, qcb->name,
					     poolsize);
	else
		ret = heapobj_init_array_shareable(&qcb->hobj, qcb->name,
						   poolsize / qlimit,
						   qlimit);
	if (ret) {
		xnfree(qcb);
		goto out;
	}

	qcb->magic = queue_magic;
	qcb->mode = mode;
	qcb->limit = qlimit;
	list_init(&qcb->mq);
	qcb->mcount = 0;

	if (mode & Q_PRIO)
		sobj_flags = SYNCOBJ_PRIO;

	syncobj_init(&qcb->sobj, sobj_flags,
		     fnref_put(libalchemy, queue_finalize));

	ret = 0;
	if (syncluster_addobj(&alchemy_queue_table, qcb->name, &qcb->cobj)) {
		heapobj_destroy(&qcb->hobj);
		syncobj_uninit(&qcb->sobj);
		xnfree(qcb);
		ret = -EEXIST;
	} else
		queue->handle = mainheap_ref(qcb, uintptr_t);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_queue_delete(RT_QUEUE *queue)
{
	struct alchemy_queue *qcb;
	struct syncstate syns;
	struct service svc;
	int ret = 0;

	if (threadobj_irq_p())
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	qcb = get_alchemy_queue(queue, &syns, &ret);
	if (qcb == NULL)
		goto out;

	syncluster_delobj(&alchemy_queue_table, &qcb->cobj);
	qcb->magic = ~queue_magic;
	syncobj_destroy(&qcb->sobj, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

void *rt_queue_alloc(RT_QUEUE *queue, size_t size)
{
	struct alchemy_queue_msg *msg = NULL;
	struct alchemy_queue *qcb;
	struct syncstate syns;
	struct service svc;
	int ret;

	COPPERPLATE_PROTECT(svc);

	qcb = get_alchemy_queue(queue, &syns, &ret);
	if (qcb == NULL)
		goto out;

	msg = heapobj_alloc(&qcb->hobj, size + sizeof(*msg));
	if (msg == NULL)
		goto done;

	/*
	 * XXX: no need to init the ->next holder, list_*pend() do not
	 * require this, and this ends up being costly on low end.
	 */
	msg->size = size;	/* Zero is allowed. */
	msg->refcount = 1;
	++msg;
done:
	put_alchemy_queue(qcb, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return msg;
}

int rt_queue_free(RT_QUEUE *queue, void *buf)
{
	struct alchemy_queue_msg *msg;
	struct alchemy_queue *qcb;
	struct syncstate syns;
	struct service svc;
	int ret = 0;

	if (buf == NULL)
		return -EINVAL;

	msg = (struct alchemy_queue_msg *)buf - 1;

	COPPERPLATE_PROTECT(svc);

	qcb = get_alchemy_queue(queue, &syns, &ret);
	if (qcb == NULL)
		goto out;

	if (heapobj_validate(&qcb->hobj, msg) == 0) {
		ret = -EINVAL;
		goto done;
	}

	/*
	 * Check the reference count under lock, so that we properly
	 * serialize with rt_queue_send() and rt_queue_receive() which
	 * may update it.
	 */
	if (msg->refcount == 0) { /* Mm, double-free? */
		ret = -EINVAL;
		goto done;
	}

	if (--msg->refcount == 0)
		heapobj_free(&qcb->hobj, msg);
done:
	put_alchemy_queue(qcb, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_queue_send(RT_QUEUE *queue,
		  const void *buf, size_t size, int mode)
{
	struct alchemy_queue_wait *wait;
	struct alchemy_queue_msg *msg;
	struct alchemy_queue *qcb;
	struct threadobj *waiter;
	struct syncstate syns;
	struct service svc;
	int ret = 0;

	if (buf == NULL)
		return -EINVAL;

	msg = (struct alchemy_queue_msg *)buf - 1;

	COPPERPLATE_PROTECT(svc);

	qcb = get_alchemy_queue(queue, &syns, &ret);
	if (qcb == NULL)
		goto out;

	if (qcb->limit && qcb->mcount >= qcb->limit) {
		ret = -ENOMEM;
		goto done;
	}

	if (msg->refcount == 0) {
		ret = -EINVAL;
		goto done;
	}

	msg->refcount--;
	msg->size = size;
	ret = 0;  /* # of tasks unblocked. */

	do {
		waiter = syncobj_post(&qcb->sobj);
		if (waiter == NULL)
			break;
		wait = threadobj_get_wait(waiter);
		wait->msg = msg;
		msg->refcount++;
		ret++;
	} while (mode & Q_BROADCAST);

	if (ret)
		goto done;
	/*
	 * We need to queue the message if no task was waiting for it,
	 * except in broadcast mode, in which case we only fix up the
	 * reference count.
	 */
	if (mode & Q_BROADCAST)
		msg->refcount++;
	else {
		qcb->mcount++;
		if (mode & Q_URGENT)
			list_prepend(&msg->next, &qcb->mq);
		else
			list_append(&msg->next, &qcb->mq);
	}
done:
	put_alchemy_queue(qcb, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_queue_write(RT_QUEUE *queue,
		   const void *buf, size_t size, int mode)
{
	struct alchemy_queue_wait *wait;
	struct alchemy_queue_msg *msg;
	struct alchemy_queue *qcb;
	struct threadobj *waiter;
	struct syncstate syns;
	int ret = 0, nwaiters;
	struct service svc;
	size_t usersz;

	if (size == 0)
		return 0;

	COPPERPLATE_PROTECT(svc);

	qcb = get_alchemy_queue(queue, &syns, &ret);
	if (qcb == NULL)
		goto out;

	waiter = syncobj_peek_at_pend(&qcb->sobj);
	if (waiter && threadobj_local_p(waiter)) {
		/*
		 * Fast path for local threads already waiting for
		 * data via rt_queue_read(): do direct copy to the
		 * reader's buffer.
		 */
		wait = threadobj_get_wait(waiter);
		usersz = wait->usersz;
		if (usersz == 0)
			/* no buffer provided, enqueue normally. */
			goto enqueue;
		if (size > usersz)
			size = usersz;
		if (size > 0)
			memcpy(wait->userbuf, buf, size);
		wait->usersz = size;
		syncobj_wakeup_waiter(&qcb->sobj, waiter);
		ret = 1;
		goto done;
	}

enqueue:
	nwaiters = syncobj_pend_count(&qcb->sobj);
	if (nwaiters == 0 && (mode & Q_BROADCAST) != 0)
		goto done;

	ret = -ENOMEM;
	if (qcb->limit && qcb->mcount >= qcb->limit)
		goto done;

	msg = heapobj_alloc(&qcb->hobj, size + sizeof(*msg));
	if (msg == NULL)
		goto done;

	msg->size = size;
	msg->refcount = 0;
	memcpy(msg + 1, buf, size);

	ret = 0;  /* # of tasks unblocked. */
	if (nwaiters == 0) {
		qcb->mcount++;
		if (mode & Q_URGENT)
			list_prepend(&msg->next, &qcb->mq);
		else
			list_append(&msg->next, &qcb->mq);
		goto done;
	}

	do {
		waiter = syncobj_post(&qcb->sobj);
		if (waiter == NULL)
			break;
		wait = threadobj_get_wait(waiter);
		wait->msg = msg;
		msg->refcount++;
		ret++;
	} while (mode & Q_BROADCAST);
done:
	put_alchemy_queue(qcb, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

ssize_t rt_queue_receive_timed(RT_QUEUE *queue, void **bufp,
			       const struct timespec *abs_timeout)
{
	struct alchemy_queue_wait *wait;
	struct alchemy_queue_msg *msg;
	struct alchemy_queue *qcb;
	struct syncstate syns;
	struct service svc;
	ssize_t ret;
	int err = 0;

	if (!threadobj_current_p() && !alchemy_poll_mode(abs_timeout))
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	qcb = get_alchemy_queue(queue, &syns, &err);
	if (qcb == NULL) {
		ret = err;
		goto out;
	}

	if (list_empty(&qcb->mq))
		goto wait;

	msg = list_pop_entry(&qcb->mq, struct alchemy_queue_msg, next);
	msg->refcount++;
	*bufp = msg + 1;
	ret = (ssize_t)msg->size;
	qcb->mcount--;
	goto done;
wait:
	if (alchemy_poll_mode(abs_timeout)) {
		ret = -EWOULDBLOCK;
		goto done;
	}

	wait = threadobj_prepare_wait(struct alchemy_queue_wait);
	wait->usersz = 0;

	ret = syncobj_pend(&qcb->sobj, abs_timeout, &syns);
	if (ret) {
		if (ret == -EIDRM) {
			threadobj_finish_wait();
			goto out;
		}
	} else {
		msg = wait->msg;
		*bufp = msg + 1;
		ret = (ssize_t)msg->size;
	}

	threadobj_finish_wait();
done:
	put_alchemy_queue(qcb, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

ssize_t rt_queue_read_timed(RT_QUEUE *queue,
			    void *buf, size_t size,
			    const struct timespec *abs_timeout)
{
	struct alchemy_queue_wait *wait;
	struct alchemy_queue_msg *msg;
	struct alchemy_queue *qcb;
	struct syncstate syns;
	struct service svc;
	ssize_t ret;
	int err = 0;

	if (!threadobj_current_p() && !alchemy_poll_mode(abs_timeout))
		return -EPERM;

	if (size == 0)
		return 0;

	COPPERPLATE_PROTECT(svc);

	qcb = get_alchemy_queue(queue, &syns, &err);
	if (qcb == NULL) {
		ret = err;
		goto out;
	}

	if (list_empty(&qcb->mq))
		goto wait;

	msg = list_pop_entry(&qcb->mq, struct alchemy_queue_msg, next);
	qcb->mcount--;
	goto transfer;
wait:
	if (alchemy_poll_mode(abs_timeout)) {
		ret = -EWOULDBLOCK;
		goto done;
	}

	wait = threadobj_prepare_wait(struct alchemy_queue_wait);
	wait->userbuf = buf;
	wait->usersz = size;
	wait->msg = NULL;

	ret = syncobj_pend(&qcb->sobj, abs_timeout, &syns);
	if (ret) {
		if (ret == -EIDRM) {
			threadobj_finish_wait();
			goto out;
		}
	} else if (wait->msg) {
		msg = wait->msg;
	transfer:
		ret = (ssize_t)(msg->size > size ? size : msg->size);
		if (ret > 0) 
			memcpy(buf, msg + 1, ret);
		heapobj_free(&qcb->hobj, msg);
	} else	/* A direct copy took place. */
		ret = (ssize_t)wait->usersz;

	threadobj_finish_wait();
done:
	put_alchemy_queue(qcb, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_queue_flush(RT_QUEUE *queue)
{
	struct alchemy_queue_msg *msg, *tmp;
	struct alchemy_queue *qcb;
	struct syncstate syns;
	struct service svc;
	int ret = 0;

	COPPERPLATE_PROTECT(svc);

	qcb = get_alchemy_queue(queue, &syns, &ret);
	if (qcb == NULL)
		goto out;

	ret = qcb->mcount;
	qcb->mcount = 0;

	/*
	 * Flushing a message queue is not an operation we should see
	 * in any fast path within an application, so locking out
	 * other threads from using that queue while we flush it is
	 * acceptable.
	 */
	if (!list_empty(&qcb->mq)) {
		list_for_each_entry_safe(msg, tmp, &qcb->mq, next) {
			list_remove(&msg->next);
			heapobj_free(&qcb->hobj, msg);
		}
	}

	put_alchemy_queue(qcb, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_queue_inquire(RT_QUEUE *queue, RT_QUEUE_INFO *info)
{
	struct alchemy_queue *qcb;
	struct syncstate syns;
	struct service svc;
	int ret = 0;

	COPPERPLATE_PROTECT(svc);

	qcb = get_alchemy_queue(queue, &syns, &ret);
	if (qcb == NULL)
		goto out;

	info->nwaiters = syncobj_pend_count(&qcb->sobj);
	info->nmessages = qcb->mcount;
	info->mode = qcb->mode;
	info->qlimit = qcb->limit;
	info->poolsize = heapobj_size(&qcb->hobj);
	info->usedmem = heapobj_inquire(&qcb->hobj);
	strcpy(info->name, qcb->name);

	put_alchemy_queue(qcb, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_queue_bind(RT_QUEUE *queue,
		  const char *name, RTIME timeout)
{
	return alchemy_bind_object(name,
				   &alchemy_queue_table,
				   timeout,
				   offsetof(struct alchemy_queue, cobj),
				   &queue->handle);
}

int rt_queue_unbind(RT_QUEUE *queue)
{
	queue->handle = 0;
	return 0;
}

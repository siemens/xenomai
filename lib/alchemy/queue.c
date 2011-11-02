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

static struct alchemy_queue *get_alchemy_queue(RT_QUEUE *queue,
					       struct syncstate *syns, int *err_r)
{
	struct alchemy_queue *qcb;

	if (queue == NULL || ((intptr_t)queue & (sizeof(intptr_t)-1)) != 0)
		goto bad_handle;

	qcb = mainheap_deref(queue->handle, struct alchemy_queue);
	if (qcb == NULL || ((intptr_t)qcb & (sizeof(intptr_t)-1)) != 0)
		goto bad_handle;

	if (qcb->magic == ~queue_magic)
		goto dead_handle;

	if (qcb->magic != queue_magic)
		goto bad_handle;

	if (syncobj_lock(&qcb->sobj, syns))
		goto bad_handle;

	/* Recheck under lock. */
	if (qcb->magic == queue_magic)
		return qcb;

dead_handle:
	/* Removed under our feet. */
	*err_r = -EIDRM;
	return NULL;

bad_handle:
	*err_r = -EINVAL;
	return NULL;
}

static inline void put_alchemy_queue(struct alchemy_queue *qcb,
				     struct syncstate *syns)
{
	syncobj_unlock(&qcb->sobj, syns);
}

static void queue_finalize(struct syncobj *sobj)
{
	struct alchemy_queue *qcb;

	qcb = container_of(sobj, struct alchemy_queue, sobj);
	heapobj_destroy(&qcb->pool);
	xnfree(qcb);
}
fnref_register(libalchemy, queue_finalize);

int rt_queue_create(RT_QUEUE *queue, const char *name,
		    size_t poolsize, size_t qlimit, int mode)
{
	struct alchemy_queue *qcb;
	struct service svc;
	int sobj_flags = 0;

	if (threadobj_async_p())
		return -EPERM;

	if (poolsize == 0)
		return -EINVAL;

	COPPERPLATE_PROTECT(svc);

	qcb = xnmalloc(sizeof(*qcb));
	if (qcb == NULL)
		goto no_mem;

	__alchemy_build_name(qcb->name, name, &queue_namegen);

	if (syncluster_addobj(&alchemy_queue_table, qcb->name, &qcb->cobj)) {
		xnfree(qcb);
		COPPERPLATE_UNPROTECT(svc);
		return -EEXIST;
	}

	/*
	 * The message pool has to be part of the main heap for proper
	 * sharing between processes.
	 */
	if (heapobj_init_shareable(&qcb->pool, NULL, poolsize)) {
		syncluster_delobj(&alchemy_queue_table, &qcb->cobj);
		xnfree(qcb);
	no_mem:
		COPPERPLATE_UNPROTECT(svc);
		return -ENOMEM;
	}

	if (mode & Q_PRIO)
		sobj_flags = SYNCOBJ_PRIO;

	qcb->magic = queue_magic;
	qcb->mode = mode;
	qcb->limit = qlimit;
	list_init(&qcb->mq);
	qcb->mcount = 0;
	syncobj_init(&qcb->sobj, sobj_flags,
		     fnref_put(libalchemy, queue_finalize));
	queue->handle = mainheap_ref(qcb, uintptr_t);

	COPPERPLATE_UNPROTECT(svc);

	return 0;
}

int rt_queue_delete(RT_QUEUE *queue)
{
	struct alchemy_queue *qcb;
	struct syncstate syns;
	struct service svc;
	int ret = 0;

	if (threadobj_async_p())
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

	msg = heapobj_alloc(&qcb->pool, size + sizeof(*msg));
	if (msg == NULL)
		goto done;

	inith(&msg->next);
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
		heapobj_free(&qcb->pool, msg);
done:
	put_alchemy_queue(qcb, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_queue_send(RT_QUEUE *queue,
		  void *buf, size_t size, int mode)
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
	void *_buf;

	_buf = rt_queue_alloc(queue, size);
	if (_buf == NULL)
		return -ENOMEM;

	if (size > 0)
		memcpy(_buf, buf, size);

	return rt_queue_send(queue, _buf, size, mode);
}

ssize_t rt_queue_receive_until(RT_QUEUE *queue,
			       void **bufp, RTIME timeout)
{
	struct alchemy_queue_wait *wait;
	struct alchemy_queue_msg *msg;
	struct timespec ts, *timespec;
	struct alchemy_queue *qcb;
	struct syncstate syns;
	struct service svc;
	ssize_t ret;
	int err = 0;

	COPPERPLATE_PROTECT(svc);

	qcb = get_alchemy_queue(queue, &syns, &err);
	if (qcb == NULL) {
		ret = err;
		goto out;
	}

	msg = list_pop_entry(&qcb->mq, struct alchemy_queue_msg, next);
	if (msg) {
		msg->refcount++;
		*bufp = msg + 1;
		ret = (ssize_t)msg->size;
		qcb->mcount--;
		goto done;
	}

	if (timeout == TM_NONBLOCK) {
		ret = -EWOULDBLOCK;
		goto done;
	}

	if (threadobj_async_p()) {
		ret = -EPERM;
		goto done;
	}

	wait = threadobj_alloc_wait(struct alchemy_queue_wait);
	if (wait == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	if (timeout != TM_INFINITE) {
		timespec = &ts;
		clockobj_ticks_to_timespec(&alchemy_clock, timeout, timespec);
	} else
		timespec = NULL;

	ret = syncobj_pend(&qcb->sobj, timespec, &syns);
	if (ret) {
		if (ret == -EIDRM) {
			threadobj_free_wait(wait);
			goto out;
		}
	} else {
		msg = wait->msg;
		*bufp = msg + 1;
		ret = (ssize_t)msg->size;
	}

	threadobj_free_wait(wait);
done:
	put_alchemy_queue(qcb, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

ssize_t rt_queue_receive(RT_QUEUE *queue,
			 void **bufp, RTIME timeout)
{
	timeout = __alchemy_rel2abs_timeout(timeout);
	return rt_queue_receive_until(queue, bufp, timeout);
}

ssize_t rt_queue_read_until(RT_QUEUE *queue,
			    void *buf, size_t size, RTIME timeout)
{
	ssize_t rsize;
	void *_buf;

	rsize = rt_queue_receive_until(queue, &_buf, timeout);
	if (rsize < 0)
		return rsize;

	if (size > rsize)
		size = rsize;

	if (size > 0)
		memcpy(buf, _buf, size);

	rt_queue_free(queue, _buf);

	return rsize;
}

ssize_t rt_queue_read(RT_QUEUE *queue,
		      void *buf, size_t size, RTIME timeout)
{
	timeout = __alchemy_rel2abs_timeout(timeout);
	return rt_queue_read_until(queue, buf, size, timeout);
}

int rt_queue_flush(RT_QUEUE *queue)
{
	struct alchemy_queue_msg *msg, *tmp;
	struct alchemy_queue *qcb;
	struct syncstate syns;
	struct service svc;
	int ret = 0, _ret;
	struct list dump;

	COPPERPLATE_PROTECT(svc);

	qcb = get_alchemy_queue(queue, &syns, &ret);
	if (qcb == NULL)
		goto out;

	/*
	 * Transfer the contents to a private list by moving list
	 * heads, so that we may free the messages without holding the
	 * queue lock.
	 */
	list_init(&dump);
	list_join(&qcb->mq, &dump);
	ret = qcb->mcount;
	qcb->mcount = 0;

	put_alchemy_queue(qcb, &syns);

	if (list_empty(&dump))
		goto out;

	list_for_each_entry_safe(msg, tmp, &dump, next) {
		/*
		 * It's a bit of a pain, but since rt_queue_delete()
		 * may run concurrently, we need to revalidate the
		 * queue descriptor for each buffer.
		 */
		_ret = rt_queue_free(queue, msg + 1);
		if (_ret) {
			ret = _ret;
			break;
		}
	}
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
	info->poolsize = 0;
	info->usedmem = 0;
	strcpy(info->name, qcb->name);

	put_alchemy_queue(qcb, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_queue_bind(RT_QUEUE *queue,
		  const char *name, RTIME timeout)
{
	return __alchemy_bind_object(name,
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

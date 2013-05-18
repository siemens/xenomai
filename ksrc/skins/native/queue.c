/**
 * @file
 * This file is part of the Xenomai project.
 *
 * @note Copyright (C) 2004 Philippe Gerum <rpm@xenomai.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * \ingroup native_queue
 */

/*!
 * \ingroup native
 * \defgroup native_queue Message queue services.
 *
 * Queue services.
 *
 * Message queueing is a method by which real-time tasks can exchange
 * or pass data through a Xenomai-managed queue of messages. Messages
 * can vary in length and be assigned different types or usages. A
 * message queue can be created by one task and used by multiple tasks
 * that send and/or receive messages to the queue.
 *
 * This implementation is based on a zero-copy scheme for message
 * buffers. Message buffer pools are built over the nucleus's heap
 * objects, which in turn provide the needed support for exchanging
 * messages between kernel and user-space using direct memory mapping.
 *
 *@{*/

/** @example msg_queue.c */

#include <nucleus/pod.h>
#include <nucleus/registry.h>
#include <native/task.h>
#include <native/queue.h>

#ifdef CONFIG_XENO_OPT_VFILE

struct vfile_priv {
	struct xnpholder *curr;
	int mode;
	size_t usable_mem;
	size_t used_mem;
	size_t limit;
	size_t count;
};

struct vfile_data {
	char name[XNOBJECT_NAME_LEN];
};

static int vfile_rewind(struct xnvfile_snapshot_iterator *it)
{
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	RT_QUEUE *q = xnvfile_priv(it->vfile);

	q = xeno_h2obj_validate(q, XENO_QUEUE_MAGIC, RT_QUEUE);
	if (q == NULL)
		return -EIDRM;

	priv->curr = getheadpq(xnsynch_wait_queue(&q->synch_base));
	priv->mode = q->mode;
	priv->usable_mem = xnheap_usable_mem(&q->bufpool);
	priv->used_mem = xnheap_used_mem(&q->bufpool);
	priv->limit = q->qlimit;
	priv->count = countq(&q->pendq);

	return xnsynch_nsleepers(&q->synch_base);
}

static int vfile_next(struct xnvfile_snapshot_iterator *it, void *data)
{
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	RT_QUEUE *q = xnvfile_priv(it->vfile);
	struct vfile_data *p = data;
	struct xnthread *thread;

	if (priv->curr == NULL)
		return 0;	/* We are done. */

	/* Fetch current waiter, advance list cursor. */
	thread = link2thread(priv->curr, plink);
	priv->curr = nextpq(xnsynch_wait_queue(&q->synch_base),
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
		xnvfile_printf(it, "%5s  %9s  %9s  %6s  %s\n",
			       "TYPE", "TOTALMEM", "USEDMEM",
			       "QLIMIT", "MCOUNT");
		xnvfile_printf(it, "%4s  %9Zu  %9Zu  %6Zu  %Zu\n",
			       priv->mode & Q_SHARED ? "shared" : "local",
			       priv->usable_mem,
			       priv->used_mem,
			       priv->limit,
			       priv->count);
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

extern struct xnptree __native_ptree;

static struct xnpnode_snapshot __q_pnode = {
	.node = {
		.dirname = "queues",
		.root = &__native_ptree,
		.ops = &xnregistry_vfsnap_ops,
	},
	.vfile = {
		.privsz = sizeof(struct vfile_priv),
		.datasz = sizeof(struct vfile_data),
		.ops = &vfile_ops,
	},
};

#else /* !CONFIG_XENO_OPT_VFILE */

static struct xnpnode_snapshot __q_pnode = {
	.node = {
		.dirname = "queues",
	},
};

#endif /* !CONFIG_XENO_OPT_VFILE */

static void __queue_flush_private(xnheap_t *heap,
				  void *poolmem, u_long poolsize, void *cookie)
{
	xnarch_free_host_mem(poolmem, poolsize);
}

/**
 * @fn int rt_queue_create(RT_QUEUE *q,const char *name,size_t poolsize,size_t qlimit,int mode)
 *
 * @brief Create a message queue.
 *
 * Create a message queue object that allows multiple tasks to
 * exchange data through the use of variable-sized messages. A message
 * queue is created empty. Message queues can be local to the kernel
 * space, or shared between kernel and user-space.
 *
 * This service needs the special character device /dev/rtheap
 * (10,254) when called from user-space tasks.
 *
 * @param q The address of a queue descriptor Xenomai will use to store
 * the queue-related data.  This descriptor must always be valid while
 * the message queue is active therefore it must be allocated in
 * permanent memory.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * queue. When non-NULL and non-empty, this string is copied to a safe
 * place into the descriptor, and passed to the registry package if
 * enabled for indexing the created queue. Shared queues must be given
 * a valid name.
 *
 * @param poolsize The size (in bytes) of the message buffer pool
 * which is going to be pre-allocated to the queue. Message buffers
 * will be claimed and released to this pool.  The buffer pool memory
 * is not extensible, so this value must be compatible with the
 * highest message pressure that could be expected.
 *
 * @param qlimit This parameter allows to limit the maximum number of
 * messages which can be queued at any point in time. Sending to a
 * full queue begets an error. The special value Q_UNLIMITED can be
 * passed to specify an unlimited amount.
 *
 * @param mode The queue creation mode. The following flags can be
 * OR'ed into this bitmask, each of them affecting the new queue:
 *
 * - Q_FIFO makes tasks pend in FIFO order on the queue for consuming
 * messages.
 *
 * - Q_PRIO makes tasks pend in priority order on the queue.
 *
 * - Q_SHARED causes the queue to be sharable between kernel and
 * user-space tasks. Otherwise, the new queue is only available for
 * kernel-based usage. This flag is implicitely set when the caller is
 * running in user-space. This feature requires the real-time support
 * in user-space to be configured in (CONFIG_XENO_OPT_PERVASIVE).
 *
 * - Q_DMA causes the buffer pool associated to the queue to be
 * allocated in physically contiguous memory, suitable for DMA
 * operations with I/O devices. A 128Kb limit exists for @a poolsize
 * when this flag is passed.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EEXIST is returned if the @a name is already in use by some
 * registered object.
 *
 * - -EINVAL is returned if @a poolsize is null, greater than the
 * system limit, or @a name is null or empty for a shared queue.
 *
 * - -ENOMEM is returned if not enough system memory is available to
 * create or register the queue. Additionally, and if Q_SHARED has
 * been passed in @a mode, errors while mapping the buffer pool in the
 * caller's address space might beget this return code too.
 *
 * - -EPERM is returned if this service was called from an invalid
 * context.
 *
 * - -ENOSYS is returned if @a mode specifies Q_SHARED, but the
 * real-time support in user-space is unavailable.
 *
 * - -ENOENT is returned if /dev/rtheap can't be opened.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - User-space task (switches to secondary mode)
 *
 * Rescheduling: possible.
 */

int rt_queue_create(RT_QUEUE *q,
		    const char *name, size_t poolsize, size_t qlimit, int mode)
{
	int err;
	spl_t s;

	if (!xnpod_root_p())
		return -EPERM;

	if (poolsize == 0)
		return -EINVAL;

#ifdef __KERNEL__
	if (mode & Q_SHARED) {
		if (!name || !*name)
			return -EINVAL;

#ifdef CONFIG_XENO_OPT_PERVASIVE
		poolsize = xnheap_rounded_size(poolsize, PAGE_SIZE);

		err = xnheap_init_mapped(&q->bufpool,
					 poolsize,
					 ((mode & Q_DMA) ? GFP_DMA
					  : XNARCH_SHARED_HEAP_FLAGS));
		if (err)
			return err;

		q->cpid = 0;
#else /* !CONFIG_XENO_OPT_PERVASIVE */
		return -ENOSYS;
#endif /* CONFIG_XENO_OPT_PERVASIVE */
	} else
#endif /* __KERNEL__ */
	{
		void *poolmem;

		poolsize = xnheap_rounded_size(poolsize, XNHEAP_PAGE_SIZE);

		poolmem = xnarch_alloc_host_mem(poolsize);

		if (!poolmem)
			return -ENOMEM;

		err = xnheap_init(&q->bufpool, poolmem, poolsize, XNHEAP_PAGE_SIZE);
		if (err) {
			xnarch_free_host_mem(poolmem, poolsize);
			return err;
		}
	}
	xnheap_set_label(&q->bufpool, "rt_queue: %s", name);

	xnsynch_init(&q->synch_base, mode & (Q_PRIO | Q_FIFO), NULL);
	initq(&q->pendq);
	q->handle = 0;		/* i.e. (still) unregistered queue. */
	q->magic = XENO_QUEUE_MAGIC;
	q->qlimit = qlimit;
	q->mode = mode;
	xnobject_copy_name(q->name, name);
	inith(&q->rlink);
	q->rqueue = &xeno_get_rholder()->queueq;
	xnlock_get_irqsave(&nklock, s);
	appendq(q->rqueue, &q->rlink);
	xnlock_put_irqrestore(&nklock, s);

	/*
	 * <!> Since xnregister_enter() may reschedule, only register
	 * complete objects, so that the registry cannot return
	 * handles to half-baked objects...
	 */
	if (name) {
		err = xnregistry_enter(q->name, q,
				       &q->handle, &__q_pnode.node);
		if (err)
			rt_queue_delete(q);
	}

	return err;
}

static void __queue_post_release(struct xnheap *heap)
{
	RT_QUEUE *q = container_of(heap, RT_QUEUE, bufpool);
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (q->handle)
		xnregistry_remove(q->handle);

	if (xnsynch_destroy(&q->synch_base) == XNSYNCH_RESCHED)
		/*
		 * Some task has been woken up as a result of
		 * the deletion: reschedule now.
		 */
		xnpod_schedule();

	xnlock_put_irqrestore(&nklock, s);

#ifdef CONFIG_XENO_OPT_PERVASIVE
	if (q->cpid)
		xnfree(q);
#endif
}

/**
 * @fn int rt_queue_delete(RT_QUEUE *q)
 *
 * @brief Delete a message queue.
 *
 * Destroy a message queue and release all the tasks currently pending
 * on it.  A queue exists in the system since rt_queue_create() has
 * been called to create it, so this service must be called in order
 * to destroy it afterwards.
 *
 * @param q The descriptor address of the affected queue.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a q is not a message queue descriptor.
 *
 * - -EIDRM is returned if @a q is a deleted queue descriptor.
 *
 * - -EPERM is returned if this service was called from an
 * asynchronous context.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - User-space task (switches to secondary mode).
 *
 * Rescheduling: possible.
 */

int rt_queue_delete_inner(RT_QUEUE *q, void __user *mapaddr)
{
	int err;
	spl_t s;

	if (xnpod_asynch_p())
		return -EPERM;

	xnlock_get_irqsave(&nklock, s);

	q = xeno_h2obj_validate(q, XENO_QUEUE_MAGIC, RT_QUEUE);

	if (!q) {
		err = xeno_handle_error(q, XENO_QUEUE_MAGIC, RT_QUEUE);
		xnlock_put_irqrestore(&nklock, s);
		return err;
	}

	xeno_mark_deleted(q);
	removeq(q->rqueue, &q->rlink);

	/* Get out of the nklocked section before releasing the heap
	   memory, since we are about to invoke Linux kernel services. */

	xnlock_put_irqrestore(&nklock, s);

	/*
	 * The queue descriptor has been marked as deleted before we
	 * released the superlock thus preventing any subsequent call
	 * to rt_queue_delete() to succeed, so now we can actually
	 * destroy the associated heap safely.
	 */

#ifdef CONFIG_XENO_OPT_PERVASIVE
	if (q->mode & Q_SHARED)
		xnheap_destroy_mapped(&q->bufpool,
				      __queue_post_release, mapaddr);
	else
#endif /* CONFIG_XENO_OPT_PERVASIVE */
	{
		xnheap_destroy(&q->bufpool, &__queue_flush_private, NULL);
		__queue_post_release(&q->bufpool);
	}

	return 0;
}

int rt_queue_delete(RT_QUEUE *q)
{
	return rt_queue_delete_inner(q, NULL);
}

/**
 * @fn void *rt_queue_alloc(RT_QUEUE *q,size_t size)
 *
 * @brief Allocate a message queue buffer.
 *
 * This service allocates a message buffer from the queue's internal
 * pool which can be subsequently filled by the caller then passed to
 * rt_queue_send() for sending.
 *
 * @param q The descriptor address of the affected queue.
 *
 * @param size The requested size in bytes of the buffer. Zero is an
 * acceptable value, meaning that the message will not carry any
 * payload data; the receiver will thus receive a zero-sized message.
 *
 * @return The address of the allocated message buffer upon success,
 * or NULL if the allocation fails.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

void *rt_queue_alloc(RT_QUEUE *q, size_t size)
{
	rt_queue_msg_t *msg;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	q = xeno_h2obj_validate(q, XENO_QUEUE_MAGIC, RT_QUEUE);

	if (!q) {
		xnlock_put_irqrestore(&nklock, s);
		return NULL;
	}

	msg =
	    (rt_queue_msg_t *) xnheap_alloc(&q->bufpool,
					    size + sizeof(rt_queue_msg_t));

	if (msg) {
		inith(&msg->link);
		msg->size = size;	/* Zero is ok. */
		msg->refcount = 1;
		++msg;
	}

	xnlock_put_irqrestore(&nklock, s);

	return msg;
}

static int __queue_check_msg(void *p)
{
	rt_queue_msg_t *msg = (rt_queue_msg_t *) p;

	if (msg->refcount == 0)
		return -EINVAL;

	if (--msg->refcount > 0)
		return -EBUSY;

	return 0;
}

/**
 * @fn int rt_queue_free(RT_QUEUE *q,void *buf)
 *
 * @brief Free a message queue buffer.
 *
 * This service releases a message buffer returned by
 * rt_queue_receive() to the queue's internal pool.
 *
 * @param q The descriptor address of the affected queue.
 *
 * @param buf The address of the message buffer to free. Even
 * zero-sized messages carrying no payload data must be freed, since
 * they are assigned a valid memory space to store internal
 * information.
 *
 * @return 0 is returned upon success, or -EINVAL if @a buf is not a
 * valid message buffer previously allocated by the rt_queue_alloc()
 * service, or the caller did not get ownership of the message through
 * a successful return from rt_queue_receive().
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

int rt_queue_free(RT_QUEUE *q, void *buf)
{
	int err;
	spl_t s;

	if (buf == NULL)
		return -EINVAL;

	xnlock_get_irqsave(&nklock, s);

	q = xeno_h2obj_validate(q, XENO_QUEUE_MAGIC, RT_QUEUE);

	if (!q) {
		err = xeno_handle_error(q, XENO_QUEUE_MAGIC, RT_QUEUE);
		goto unlock_and_exit;
	}

	err = xnheap_test_and_free(&q->bufpool,
				   ((rt_queue_msg_t *) buf) - 1,
				   &__queue_check_msg);
	if (err == -EBUSY)
		/* Release failed due to non-zero refcount; this is not an
		 * error from the interface POV. */
		err = 0;

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

/**
 * @fn int rt_queue_send(RT_QUEUE *q,void *mbuf,size_t size,int mode)
 *
 * @brief Send a message to a queue.
 *
 * This service sends a complete message to a given queue. The message
 * must have been allocated by a previous call to rt_queue_alloc().
 *
 * @param q The descriptor address of the message queue to send to.
 *
 * @param mbuf The address of the message buffer to be sent.  The
 * message buffer must have been allocated using the rt_queue_alloc()
 * service.  Once passed to rt_queue_send(), the memory pointed to by
 * @a mbuf is no more under the control of the sender and thus should
 * not be referenced by it anymore; deallocation of this memory must
 * be handled on the receiving side.
 *
 * @param size The size in bytes of the message. Zero is a valid
 * value, in which case an empty message will be sent.
 *
 * @param mode A set of flags affecting the operation:
 *
 * - Q_URGENT causes the message to be prepended to the message queue,
 * ensuring a LIFO ordering.
 *
 * - Q_NORMAL causes the message to be appended to the message queue,
 * ensuring a FIFO ordering.
 *
 * - Q_BROADCAST causes the message to be sent to all tasks currently
 * waiting for messages. The message is not copied; a reference count
 * is maintained instead so that the message will remain valid until
 * the last receiver releases its own reference using rt_queue_free(),
 * after which the message space will be returned to the queue's
 * internal pool.
 *
 * @return Upon success, this service returns the number of receivers
 * which got awaken as a result of the operation. If zero is returned,
 * no task was waiting on the receiving side of the queue, and the
 * message has been enqueued. Upon error, one of the following error
 * codes is returned:
 *
 * - -EINVAL is returned if @a q is not a message queue descriptor, or
 * @a mbuf is not a valid message buffer obtained from a previous call
 * to rt_queue_alloc().
 *
 * - -EIDRM is returned if @a q is a deleted queue descriptor.
 *
 * - -ENOMEM is returned if queuing the message would exceed the limit
 * defined for the queue at creation.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: possible.
 */

int rt_queue_send(RT_QUEUE *q, void *mbuf, size_t size, int mode)
{
	xnthread_t *sleeper;
	rt_queue_msg_t *msg;
	int err, nrecv = 0;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	q = xeno_h2obj_validate(q, XENO_QUEUE_MAGIC, RT_QUEUE);

	if (!q) {
		err = xeno_handle_error(q, XENO_QUEUE_MAGIC, RT_QUEUE);
		goto unlock_and_exit;
	}

	if (q->qlimit != Q_UNLIMITED && countq(&q->pendq) >= q->qlimit) {
		err = -ENOMEM;
		goto unlock_and_exit;
	}

	msg = ((rt_queue_msg_t *) mbuf) - 1;

	if (xnheap_check_block(&q->bufpool, msg) || msg->refcount == 0) {
		/* In case of invalid block or if the sender does not own the
		   message, just bail out. */
		err = -EINVAL;
		goto unlock_and_exit;
	}

	/* Message buffer ownership is being transferred from the sender to
	   the receiver(s) here; so we need to update the reference count
	   appropriately. */
	msg->refcount--;
	msg->size = size;

	do {
		sleeper = xnsynch_wakeup_one_sleeper(&q->synch_base);

		if (!sleeper)
			break;

		sleeper->wait_u.buffer.ptr = msg;
		msg->refcount++;
		nrecv++;
	}
	while (mode & Q_BROADCAST);

	if (nrecv > 0)
		xnpod_schedule();
	else if (!(mode & Q_BROADCAST)) {
		/* Messages are never queued in broadcast mode. Otherwise we
		   need to queue the message if no task is waiting for it. */

		if (mode & Q_URGENT)
			prependq(&q->pendq, &msg->link);
		else
			appendq(&q->pendq, &msg->link);
	} else
		/* Ownership did not change, so update reference count. */
		msg->refcount++;

	err = nrecv;

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

/**
 * @fn int rt_queue_write(RT_QUEUE *q,const void *buf,size_t size,int mode)
 *
 * @brief Write a message to a queue.
 *
 * This service writes a complete message to a given queue. This
 * service differs from rt_queue_send() in that it accepts a pointer
 * to the raw data to be sent, instead of a canned message
 * buffer.
 *
 * @param q The descriptor address of the message queue to write to.
 *
 * @param buf The address of the message data to be written to the
 * queue. A message buffer will be allocated internally to convey the
 * data.
 *
 * @param size The size in bytes of the message data. Zero is a valid
 * value, in which case an empty message will be sent.
 *
 * @param mode A set of flags affecting the operation:
 *
 * - Q_URGENT causes the message to be prepended to the message queue,
 * ensuring a LIFO ordering.
 *
 * - Q_NORMAL causes the message to be appended to the message queue,
 * ensuring a FIFO ordering.
 *
 * - Q_BROADCAST causes the message to be sent to all tasks currently
 * waiting for messages. The message is not copied; a reference count
 * is maintained instead so that the message will remain valid until
 * all receivers get a copy of the message, after which the message
 * space will be returned to the queue's internal pool.
 *
 * @return Upon success, this service returns the number of receivers
 * which got awaken as a result of the operation. If zero is returned,
 * no task was waiting on the receiving side of the queue, and the
 * message has been enqueued. Upon error, one of the following error
 * codes is returned:
 *
 * - -EINVAL is returned if @a q is not a message queue descriptor.
 *
 * - -EIDRM is returned if @a q is a deleted queue descriptor.
 *
 * - -ENOMEM is returned if queuing the message would exceed the limit
 * defined for the queue at creation, or if no memory can be obtained
 * to convey the message data internally.
 *
 * - -ESRCH is returned if a @a q represents a stale userland handle
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: possible.
 */

int rt_queue_write(RT_QUEUE *q, const void *buf, size_t size, int mode)
{
	void *mbuf = rt_queue_alloc(q, size);

	if (!mbuf)
		return -ENOMEM;

	if (size > 0)
		memcpy(mbuf, buf, size);

	return rt_queue_send(q, mbuf, size, mode);
}

ssize_t rt_queue_receive_inner(RT_QUEUE *q, void **bufp,
			       xntmode_t timeout_mode, RTIME timeout)
{
	rt_queue_msg_t *msg = NULL;
	xnthread_t *thread;
	xnholder_t *holder;
	ssize_t err = 0;
	xnflags_t info;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	q = xeno_h2obj_validate(q, XENO_QUEUE_MAGIC, RT_QUEUE);

	if (!q) {
		err = xeno_handle_error(q, XENO_QUEUE_MAGIC, RT_QUEUE);
		goto unlock_and_exit;
	}

	holder = getq(&q->pendq);

	if (holder) {
		msg = link2rtmsg(holder);
		msg->refcount++;
	} else {
		if (timeout == TM_NONBLOCK) {
			err = -EWOULDBLOCK;;
			goto unlock_and_exit;
		}

		if (xnpod_unblockable_p()) {
			err = -EPERM;
			goto unlock_and_exit;
		}

		info = xnsynch_sleep_on(&q->synch_base,
					timeout, timeout_mode);
		if (info & XNRMID)
			err = -EIDRM;	/* Queue deleted while pending. */
		else if (info & XNTIMEO)
			err = -ETIMEDOUT; /* Timeout. */
		else if (info & XNBREAK)
			err = -EINTR;	/* Unblocked. */
		else {
			thread = xnpod_current_thread();
			msg = thread->wait_u.buffer.ptr;
			thread->wait_u.buffer.ptr = NULL;
		}
	}

	if (msg) {
		*bufp = msg + 1;
		err = (ssize_t) msg->size;
	}

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

/**
 * @fn ssize_t rt_queue_receive(RT_QUEUE *q,void **bufp,RTIME timeout)
 *
 * @brief Receive a message from a queue.
 *
 * This service retrieves the next message available from the given
 * queue. Unless otherwise specified, the caller is blocked for a
 * given amount of time if no message is immediately available on
 * entry.
 *
 * @param q The descriptor address of the message queue to receive
 * from.
 *
 * @param bufp A pointer to a memory location which will be written
 * upon success with the address of the received message. Once
 * consumed, the message space should be freed using rt_queue_free().
 *
 * @param timeout The number of clock ticks to wait for a message to
 * arrive (see note). Passing TM_INFINITE causes the caller to block
 * indefinitely until some message is eventually available. Passing
 * TM_NONBLOCK causes the service to return immediately without
 * waiting if no message is available on entry.
 *
 * @return The number of bytes available from the received message is
 * returned upon success. Zero is a possible value corresponding to a
 * zero-sized message passed to rt_queue_send(). Otherwise:
 *
 * - -EINVAL is returned if @a q is not a message queue descriptor.
 *
 * - -EIDRM is returned if @a q is a deleted queue descriptor.
 *
 * - -ETIMEDOUT is returned if @a timeout is different from
 * TM_NONBLOCK and no message is available within the specified amount
 * of time.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to TM_NONBLOCK
 * and no message is immediately available on entry.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * waiting task before any data was available.
 *
 * - -EPERM is returned if this service should block, but was called
 * from a context which cannot sleep (e.g. interrupt, non-realtime
 * context).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 *   only if @a timeout is equal to TM_NONBLOCK.
 *
 * - Kernel-based task
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always unless the request is immediately satisfied or
 * @a timeout specifies a non-blocking operation.
 *
 * @note The @a timeout value will be interpreted as jiffies if the
 * native skin is bound to a periodic time base (see
 * CONFIG_XENO_OPT_NATIVE_PERIOD), or nanoseconds otherwise.
 */

ssize_t rt_queue_receive(RT_QUEUE *q, void **bufp, RTIME timeout)
{
	return rt_queue_receive_inner(q, bufp, XN_RELATIVE, timeout);
}

/**
 * @fn ssize_t rt_queue_receive_until(RT_QUEUE *q,void **bufp,RTIME timeout)
 *
 * @brief Receive a message from a queue (with absolute timeout date).
 *
 * This service retrieves the next message available from the given
 * queue. Unless otherwise specified, the caller is blocked for a
 * given amount of time if no message is immediately available on
 * entry.
 *
 * @param q The descriptor address of the message queue to receive
 * from.
 *
 * @param bufp A pointer to a memory location which will be written
 * upon success with the address of the received message. Once
 * consumed, the message space should be freed using rt_queue_free().
 *
 * @param timeout The absolute date specifying a time limit to wait
 * for a message to arrive (see note). Passing TM_INFINITE causes the
 * caller to block indefinitely until some message is eventually
 * available. Passing TM_NONBLOCK causes the service to return
 * immediately without waiting if no message is available on entry.
 *
 * @return The number of bytes available from the received message is
 * returned upon success. Zero is a possible value corresponding to a
 * zero-sized message passed to rt_queue_send(). Otherwise:
 *
 * - -EINVAL is returned if @a q is not a message queue descriptor.
 *
 * - -EIDRM is returned if @a q is a deleted queue descriptor.
 *
 * - -ETIMEDOUT is returned if the absolute @a timeout date is reached
 * before a message arrives.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to TM_NONBLOCK
 * and no message is immediately available on entry.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * waiting task before any data was available.
 *
 * - -EPERM is returned if this service should block, but was called
 * from a context which cannot sleep (e.g. interrupt, non-realtime
 * context).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 *   only if @a timeout is equal to TM_NONBLOCK.
 *
 * - Kernel-based task
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always unless the request is immediately satisfied or
 * @a timeout specifies a non-blocking operation.
 *
 * @note The @a timeout value will be interpreted as jiffies if the
 * native skin is bound to a periodic time base (see
 * CONFIG_XENO_OPT_NATIVE_PERIOD), or nanoseconds otherwise.
 */

ssize_t rt_queue_receive_until(RT_QUEUE *q, void **bufp, RTIME timeout)
{
	return rt_queue_receive_inner(q, bufp, XN_REALTIME, timeout);
}

ssize_t rt_queue_read_inner(RT_QUEUE *q, void *buf,
			    size_t size, xntmode_t timeout_mode, RTIME timeout)
{
	ssize_t rsize;
	void *mbuf;

	rsize = rt_queue_receive_inner(q, &mbuf, timeout_mode, timeout);

	if (rsize < 0)
		return rsize;

	if (size > rsize)
		size = rsize;

	if (size > 0)
		memcpy(buf, mbuf, size);

	rt_queue_free(q, mbuf);

	return rsize;
}

/**
 * @fn ssize_t rt_queue_read(RT_QUEUE *q,void *buf,size_t size,RTIME timeout)
 *
 * @brief Read a message from a queue.
 *
 * This service retrieves the next message available from the given
 * queue. Unless otherwise specified, the caller is blocked for a
 * given amount of time if no message is immediately available on
 * entry. This services differs from rt_queue_receive() in that it
 * copies back the payload data to a user-defined memory area, instead
 * of returning a pointer to the message buffer holding such data.
 *
 * @param q The descriptor address of the message queue to read
 * from.
 *
 * @param buf A pointer to a memory area which will be written upon
 * success with the message contents. The internal message buffer
 * conveying the data is automatically freed by this call.
 *
 * @param size The length in bytes of the memory area pointed to by @a
 * buf. Messages larger than @a size are truncated appropriately.
 *
 * @param timeout The number of clock ticks to wait for a message to
 * arrive (see note). Passing TM_INFINITE causes the caller to block
 * indefinitely until some message is eventually available. Passing
 * TM_NONBLOCK causes the service to return immediately without
 * waiting if no message is available on entry.
 *
 * @return The number of bytes available from the received message is
 * returned upon success, which might be greater than the actual
 * number of bytes copied to the destination buffer if the message has
 * been truncated. Zero is a possible value corresponding to a
 * zero-sized message passed to rt_queue_send() or
 * rt_queue_write(). Otherwise:
 *
 * - -EINVAL is returned if @a q is not a message queue descriptor.
 *
 * - -EIDRM is returned if @a q is a deleted queue descriptor.
 *
 * - -ETIMEDOUT is returned if @a timeout is different from
 * TM_NONBLOCK and no message is available within the specified amount
 * of time.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to TM_NONBLOCK
 * and no message is immediately available on entry.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * waiting task before any data was available.
 *
 * - -EPERM is returned if this service should block, but was called
 * from a context which cannot sleep (e.g. interrupt, non-realtime
 * context).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 *   only if @a timeout is equal to TM_NONBLOCK.
 *
 * - Kernel-based task
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always unless the request is immediately satisfied or
 * @a timeout specifies a non-blocking operation.
 *
 * @note The @a timeout value will be interpreted as jiffies if the
 * native skin is bound to a periodic time base (see
 * CONFIG_XENO_OPT_NATIVE_PERIOD), or nanoseconds otherwise.
 */

ssize_t rt_queue_read(RT_QUEUE *q, void *buf, size_t size, RTIME timeout)
{
	return rt_queue_read_inner(q, buf, size, XN_RELATIVE, timeout);
}

/**
 * @fn ssize_t rt_queue_read_until(RT_QUEUE *q,void *buf,size_t size,RTIME timeout)
 *
 * @brief Read a message from a queue (with absolute timeout date).
 *
 * This service retrieves the next message available from the given
 * queue. Unless otherwise specified, the caller is blocked for a
 * given amount of time if no message is immediately available on
 * entry. This services differs from rt_queue_receive() in that it
 * copies back the payload data to a user-defined memory area, instead
 * of returning a pointer to the message buffer holding such data.
 *
 * @param q The descriptor address of the message queue to read
 * from.
 *
 * @param buf A pointer to a memory area which will be written upon
 * success with the message contents. The internal message buffer
 * conveying the data is automatically freed by this call.
 *
 * @param size The length in bytes of the memory area pointed to by @a
 * buf. Messages larger than @a size are truncated appropriately.
 *
 * @param timeout The absolute date specifying a time limit to wait
 * for a message to arrive (see note). Passing TM_INFINITE causes the
 * caller to block indefinitely until some message is eventually
 * available. Passing TM_NONBLOCK causes the service to return
 * immediately without waiting if no message is available on entry.
 *
 * @return The number of bytes available from the received message is
 * returned upon success, which might be greater than the actual
 * number of bytes copied to the destination buffer if the message has
 * been truncated. Zero is a possible value corresponding to a
 * zero-sized message passed to rt_queue_send() or
 * rt_queue_write(). Otherwise:
 *
 * - -EINVAL is returned if @a q is not a message queue descriptor.
 *
 * - -EIDRM is returned if @a q is a deleted queue descriptor.
 *
 * - -ETIMEDOUT is returned if the absolute @a timeout date is reached
 * before a message arrives.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to TM_NONBLOCK
 * and no message is immediately available on entry.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * waiting task before any data was available.
 *
 * - -EPERM is returned if this service should block, but was called
 * from a context which cannot sleep (e.g. interrupt, non-realtime
 * context).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 *   only if @a timeout is equal to TM_NONBLOCK.
 *
 * - Kernel-based task
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always unless the request is immediately satisfied or
 * @a timeout specifies a non-blocking operation.
 *
 * @note The @a timeout value will be interpreted as jiffies if the
 * native skin is bound to a periodic time base (see
 * CONFIG_XENO_OPT_NATIVE_PERIOD), or nanoseconds otherwise.
 */

ssize_t rt_queue_read_until(RT_QUEUE *q, void *buf, size_t size, RTIME timeout)
{
	return rt_queue_read_inner(q, buf, size, XN_REALTIME, timeout);
}

/**
 * @fn int rt_queue_flush(RT_QUEUE *q)
 *
 * @brief Flush a message queue.
 *
 * This service discards all unread messages from a message queue.
 *
 * @param q The descriptor address of the affected queue.
 *
 * @return The number of messages flushed is returned upon
 * success. Otherwise:
 *
 * - -EINVAL is returned if @a q is not a message queue descriptor.
 *
 * - -EIDRM is returned if @a q is a deleted queue descriptor.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

int rt_queue_flush(RT_QUEUE *q)
{
	struct rt_queue_msg *msg;
	struct xnqueue tmpq;
	struct xnholder *h;
	int ret, count;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	q = xeno_h2obj_validate(q, XENO_QUEUE_MAGIC, RT_QUEUE);
	if (q == NULL) {
		ret = xeno_handle_error(q, XENO_QUEUE_MAGIC, RT_QUEUE);
		xnlock_put_irqrestore(&nklock, s);
		return ret;
	}

	/*
	 * Transfer the contents to a private queue by moving queue
	 * heads, so that we may free any number of messages without
	 * holding the nklock.
	 */
	initq(&tmpq);
	moveq(&tmpq, &q->pendq);

	xnlock_put_irqrestore(&nklock, s);

	count = countq(&tmpq);

	while ((h = getq(&tmpq)) != NULL) {
		msg = link2rtmsg(h);
		/*
		 * It's a bit of a pain, but since rt_queue_delete()
		 * may run concurrently, we need to revalidate the
		 * queue descriptor for each buffer.
		 */
		msg->refcount = 1;
		ret = rt_queue_free(q, msg + 1);
		if (ret)
			break;
	}

	return count;
}

/**
 * @fn int rt_queue_inquire(RT_QUEUE *q, RT_QUEUE_INFO *info)
 *
 * @brief Inquire about a message queue.
 *
 * Return various information about the status of a given queue.
 *
 * @param q The descriptor address of the inquired queue.
 *
 * @param info The address of a structure the queue information will
 * be written to.

 * @return 0 is returned and status information is written to the
 * structure pointed at by @a info upon success. Otherwise:
 *
 * - -EINVAL is returned if @a q is not a message queue descriptor.
 *
 * - -EIDRM is returned if @a q is a deleted queue descriptor.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

int rt_queue_inquire(RT_QUEUE *q, RT_QUEUE_INFO *info)
{
	int err = 0;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	q = xeno_h2obj_validate(q, XENO_QUEUE_MAGIC, RT_QUEUE);

	if (!q) {
		err = xeno_handle_error(q, XENO_QUEUE_MAGIC, RT_QUEUE);
		goto unlock_and_exit;
	}

	strcpy(info->name, q->name);
	info->nwaiters = xnsynch_nsleepers(&q->synch_base);
	info->nmessages = countq(&q->pendq);
	info->qlimit = q->qlimit;
	info->poolsize = xnheap_usable_mem(&q->bufpool);
	info->usedmem = xnheap_used_mem(&q->bufpool);
	info->mode = q->mode;

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

/**
 * @fn int rt_queue_bind(RT_QUEUE *q,const char *name,RTIME timeout)
 *
 * @brief Bind to a shared message queue.
 *
 * This user-space only service retrieves the uniform descriptor of a
 * given shared Xenomai message queue identified by its symbolic name. If
 * the queue does not exist on entry, this service blocks the caller
 * until a queue of the given name is created.
 *
 * @param name A valid NULL-terminated name which identifies the
 * queue to bind to.
 *
 * @param q The address of a queue descriptor retrieved by the
 * operation. Contents of this memory is undefined upon failure.
 *
 * @param timeout The number of clock ticks to wait for the
 * registration to occur (see note). Passing TM_INFINITE causes the
 * caller to block indefinitely until the object is
 * registered. Passing TM_NONBLOCK causes the service to return
 * immediately without waiting if the object is not registered on
 * entry.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EFAULT is returned if @a q or @a name is referencing invalid
 * memory.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * waiting task before the retrieval has completed.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to TM_NONBLOCK
 * and the searched object is not registered on entry.
 *
 * - -ETIMEDOUT is returned if the object cannot be retrieved within
 * the specified amount of time.
 *
 * - -EPERM is returned if this service should block, but was called
 * from a context which cannot sleep (e.g. interrupt, non-realtime
 * context).  This error may also be returned whenever the call
 * attempts to bind from a user-space application to a local queue
 * defined from kernel space (i.e. Q_SHARED was not passed to
 * rt_queue_create()).
 *
 * - -ENOENT is returned if the special file /dev/rtheap
 * (character-mode, major 10, minor 254) is not available from the
 * filesystem. This device is needed to map the memory pool used by
 * the shared queue into the caller's address space. udev-based
 * systems should not need manual creation of such device entry.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always unless the request is immediately satisfied or
 * @a timeout specifies a non-blocking operation.
 *
 * @note The @a timeout value will be interpreted as jiffies if the
 * native skin is bound to a periodic time base (see
 * CONFIG_XENO_OPT_NATIVE_PERIOD), or nanoseconds otherwise.
 */

/**
 * @fn int rt_queue_unbind(RT_QUEUE *q)
 *
 * @brief Unbind from a shared message queue.
 *
 * This user-space only service unbinds the calling task from the
 * message queue object previously retrieved by a call to
 * rt_queue_bind().
 *
 * Unbinding from a message queue when it is no more needed is
 * especially important in order to properly release the mapping
 * resources used to attach the shared queue memory to the caller's
 * address space.
 *
 * @param q The address of a queue descriptor to unbind from.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a q is invalid or not bound.
 *
 * This service can be called from:
 *
 * - User-space task.
 *
 * Rescheduling: never.
 */

int __native_queue_pkg_init(void)
{
	return 0;
}

void __native_queue_pkg_cleanup(void)
{
	__native_queue_flush_rq(&__native_global_rholder.queueq);
}

/*@}*/

EXPORT_SYMBOL_GPL(rt_queue_create);
EXPORT_SYMBOL_GPL(rt_queue_delete);
EXPORT_SYMBOL_GPL(rt_queue_alloc);
EXPORT_SYMBOL_GPL(rt_queue_free);
EXPORT_SYMBOL_GPL(rt_queue_send);
EXPORT_SYMBOL_GPL(rt_queue_write);
EXPORT_SYMBOL_GPL(rt_queue_receive);
EXPORT_SYMBOL_GPL(rt_queue_receive_until);
EXPORT_SYMBOL_GPL(rt_queue_read);
EXPORT_SYMBOL_GPL(rt_queue_read_until);
EXPORT_SYMBOL_GPL(rt_queue_flush);
EXPORT_SYMBOL_GPL(rt_queue_inquire);

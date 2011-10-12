/**
 * @file
 * This file is part of the Xenomai project.
 *
 * @note Copyright (C) 2008 Philippe Gerum <rpm@xenomai.org>
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
 * \ingroup buffer
 */

/*!
 * \ingroup native
 * \defgroup buffer Buffer services.
 *
 * Buffer services.
 *
 * A buffer is a lightweight IPC object, implementing a fast, one-way
 * Producer-Consumer data path. All messages written are buffered in a
 * single memory area in strict FIFO order, until read either in
 * blocking or non-blocking mode.
 *
 * Message are always atomically handled on the write side (i.e. no
 * interleave, no short writes), whilst only complete messages are
 * normally returned to the read side. However, short reads may happen
 * under a well-defined situation (see note in rt_buffer_read()),
 * albeit they can be fully avoided by proper use of the buffer.
 *
 *@{*/

#include <nucleus/pod.h>
#include <nucleus/registry.h>
#include <nucleus/heap.h>
#include <nucleus/bufd.h>
#include <native/task.h>
#include <native/buffer.h>
#include <native/timer.h>

#ifdef CONFIG_XENO_OPT_VFILE

struct vfile_priv {
	struct xnpholder *curr;
	int mode;
	size_t bufsz;
	size_t fillsz;
	int input;
};

struct vfile_data {
	char name[XNOBJECT_NAME_LEN];
	int input;
};

static int vfile_rewind(struct xnvfile_snapshot_iterator *it)
{
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	RT_BUFFER *bf = xnvfile_priv(it->vfile);

	bf = xeno_h2obj_validate(bf, XENO_BUFFER_MAGIC, RT_BUFFER);
	if (bf == NULL)
		return -EIDRM;

	/* Start collecting records from the input wait side. */
	priv->curr = getheadpq(xnsynch_wait_queue(&bf->isynch_base));
	priv->mode = bf->mode;
	priv->bufsz = bf->bufsz;
	priv->fillsz = bf->fillsz;
	priv->input = 1;

	return xnsynch_nsleepers(&bf->isynch_base) +
		xnsynch_nsleepers(&bf->osynch_base);
}

static int vfile_next(struct xnvfile_snapshot_iterator *it, void *data)
{
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	RT_BUFFER *bf = xnvfile_priv(it->vfile);
	struct vfile_data *p = data;
	struct xnthread *thread;
	struct xnpqueue *waitq;

	if (priv->curr == NULL) { /* Attempt to switch queues. */
		if (!priv->input)
			/* Finished output side, we are done. */
			return 0;
		priv->input = 0;
		waitq = xnsynch_wait_queue(&bf->osynch_base);
		priv->curr = getheadpq(waitq);
		if (priv->curr == NULL)
			return 0;
	} else
		waitq = priv->input ? xnsynch_wait_queue(&bf->isynch_base) :
			xnsynch_wait_queue(&bf->osynch_base);

	/* Fetch current waiter, advance list cursor. */
	thread = link2thread(priv->curr, plink);
	priv->curr = nextpq(waitq, priv->curr);
	/* Collect thread name to be output in ->show(). */
	strncpy(p->name, xnthread_name(thread), sizeof(p->name));
	p->input = priv->input;

	return 1;
}

static int vfile_show(struct xnvfile_snapshot_iterator *it, void *data)
{
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	struct vfile_data *p = data;

	if (p == NULL) {	/* Dump header. */
		xnvfile_printf(it, "%4s  %9s  %9s\n",
			       "TYPE", "TOTALMEM", "USEDMEM");
		xnvfile_printf(it, "%s  %9Zu  %9Zu\n",
			       priv->mode & B_PRIO ? "PRIO" : "FIFO",
			       priv->bufsz, priv->fillsz);
		if (it->nrdata > 0)
			/* Buffer is pended -- dump waiters */
			xnvfile_printf(it, "\n%3s  %s\n", "WAY", "WAITER");
	} else
		xnvfile_printf(it, "%3s  %.*s\n",
			       p->input ? "in" : "out",
			       (int)sizeof(p->name), p->name);

	return 0;
}

static struct xnvfile_snapshot_ops vfile_ops = {
	.rewind = vfile_rewind,
	.next = vfile_next,
	.show = vfile_show,
};

extern struct xnptree __native_ptree;

static struct xnpnode_snapshot __buffer_pnode = {
	.node = {
		.dirname = "buffers",
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

static struct xnpnode_snapshot __buffer_pnode = {
	.node = {
		.dirname = "buffers",
	},
};

#endif /* !CONFIG_XENO_OPT_VFILE */

/**
 * @fn int rt_buffer_create(RT_BUFFER *bf, const char *name, size_t bufsz, int mode)
 * @brief Create a buffer.
 *
 * Create a synchronization object that allows tasks to send and
 * receive data asynchronously via a memory buffer. Data may be of an
 * arbitrary length, albeit this IPC is best suited for small to
 * medium-sized messages, since data always have to be copied to the
 * buffer during transit. Large messages may be more efficiently
 * handled by message queues (RT_QUEUE) via
 * rt_queue_send()/rt_queue_receive() services.
 *
 * @param bf The address of a buffer descriptor Xenomai will use to
 * store the buffer-related data.  This descriptor must always be
 * valid while the buffer is active therefore it must be allocated in
 * permanent memory.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * buffer. When non-NULL and non-empty, this string is copied to a
 * safe place into the descriptor, and passed to the registry package
 * if enabled for indexing the created buffer.
 *
 * @param bufsz The size of the buffer space available to hold
 * data. The required memory is obtained from the system heap.
 *
 * @param mode The buffer creation mode. The following flags can be
 * OR'ed into this bitmask, each of them affecting the new buffer:
 *
 * - B_FIFO makes tasks pend in FIFO order for reading data from the
 *   buffer.
 *
 * - B_PRIO makes tasks pend in priority order for reading data from
 *   the buffer.
 *
 * This parameter also applies to tasks blocked on the buffer's output
 * queue (see rt_buffer_write()).
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -ENOMEM is returned if the system fails to get enough dynamic
 * memory from the global real-time heap in order to register the
 * buffer.
 *
 * - -EEXIST is returned if the @a name is already in use by some
 * registered object.
 *
 * - -EPERM is returned if this service was called from an
 * asynchronous context.
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

int rt_buffer_create(RT_BUFFER *bf, const char *name, size_t bufsz, int mode)
{
	int ret = 0;
	spl_t s;

	if (xnpod_asynch_p())
		return -EPERM;

	if (bufsz == 0)
		return -EINVAL;

	bf->bufmem = xnarch_alloc_host_mem(bufsz);
	if (bf->bufmem == NULL)
		return -ENOMEM;

	xnsynch_init(&bf->isynch_base, mode & B_PRIO, NULL);
	xnsynch_init(&bf->osynch_base, mode & B_PRIO, NULL);

	bf->handle = 0;	/* i.e. (still) unregistered buffer. */
	xnobject_copy_name(bf->name, name);
	inith(&bf->rlink);
	bf->rqueue = &xeno_get_rholder()->bufferq;
	xnlock_get_irqsave(&nklock, s);
	appendq(bf->rqueue, &bf->rlink);
	xnlock_put_irqrestore(&nklock, s);

	bf->mode = mode;
	bf->bufsz = bufsz;
	bf->rdoff = 0;
	bf->wroff = 0;
	bf->fillsz = 0;
	bf->rdtoken = 0;
	bf->wrtoken = 0;

#ifdef CONFIG_XENO_OPT_PERVASIVE
	bf->cpid = 0;
#endif /* CONFIG_XENO_OPT_PERVASIVE */
	bf->magic = XENO_BUFFER_MAGIC;

	/*
	 * <!> Since xnregister_enter() may reschedule, only register
	 * complete objects, so that the registry cannot return
	 * handles to half-baked objects...
	 */
	if (name) {
		ret = xnregistry_enter(bf->name, bf, &bf->handle,
				       &__buffer_pnode.node);

		if (ret)
			rt_buffer_delete(bf);
	}

	return ret;
}

/**
 * @fn int rt_buffer_delete(RT_BUFFER *bf)
 * @brief Delete a buffer.
 *
 * Destroy a buffer and release all the tasks currently
 * pending on it.  A buffer exists in the system since
 * rt_buffer_create() has been called to create it, so this service must
 * be called in order to destroy it afterwards.
 *
 * @param bf The descriptor address of the buffer to delete.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a bf is not a buffer descriptor.
 *
 * - -EIDRM is returned if @a bf is a deleted buffer descriptor.
 *
 * - -EPERM is returned if this service was called from an
 * asynchronous context.
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

int rt_buffer_delete(RT_BUFFER *bf)
{
	int ret = 0, resched;
	void *bufmem = NULL;
	size_t bufsz = 0;
	spl_t s;

	if (xnpod_asynch_p())
		return -EPERM;

	xnlock_get_irqsave(&nklock, s);

	bf = xeno_h2obj_validate(bf, XENO_BUFFER_MAGIC, RT_BUFFER);
	if (bf == NULL) {
		ret = xeno_handle_error(bf, XENO_BUFFER_MAGIC, RT_BUFFER);
		goto unlock_and_exit;
	}

	bufmem = bf->bufmem;
	bufsz = bf->bufsz;
	removeq(bf->rqueue, &bf->rlink);
	resched = xnsynch_destroy(&bf->isynch_base) == XNSYNCH_RESCHED;
	resched += xnsynch_destroy(&bf->osynch_base) == XNSYNCH_RESCHED;

	if (bf->handle)
		xnregistry_remove(bf->handle);

	xeno_mark_deleted(bf);

	if (resched)
		/*
		 * Some task has been woken up as a result of the
		 * deletion: reschedule now.
		 */
		xnpod_schedule();

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	if (bufmem)
		xnarch_free_host_mem(bufmem, bufsz);

	return ret;
}

ssize_t rt_buffer_write_inner(RT_BUFFER *bf,
			      struct xnbufd *bufd,
			      xntmode_t timeout_mode, RTIME timeout)
{
	xnthread_t *thread, *waiter;
	size_t len, rbytes, n;
	xnflags_t info;
	u_long wrtoken;
	off_t wroff;
	ssize_t ret;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	bf = xeno_h2obj_validate(bf, XENO_BUFFER_MAGIC, RT_BUFFER);
	if (bf == NULL) {
		ret = xeno_handle_error(bf, XENO_BUFFER_MAGIC, RT_BUFFER);
		goto unlock_and_exit;
	}

	/*
	 * We may only send complete messages, so there is no point in
	 * accepting messages which are larger than what the buffer
	 * can hold.
	 */
	len = bufd->b_len;
	if (len > bf->bufsz) {
		ret = -EINVAL;
		goto unlock_and_exit;
	}

	if (len == 0) {
		ret = 0;
		goto unlock_and_exit;
	}

	if (timeout_mode == XN_RELATIVE &&
	    timeout != TM_NONBLOCK && timeout != TM_INFINITE) {
		/*
		 * We may sleep several times before being able to
		 * send the data, so let's always use an absolute time
		 * spec.
		 */
		timeout_mode = XN_REALTIME;
		timeout += xntbase_get_time(__native_tbase);
	}

redo:
	for (;;) {
		/*
		 * We should be able to write the entire message at
		 * once, or block.
		 */
		if (bf->fillsz + len > bf->bufsz)
			goto wait;

		/*
		 * Draw the next write token so that we can later
		 * detect preemption.
		 */
		wrtoken = ++bf->wrtoken;

		/* Write to the buffer in a circular way. */
		wroff = bf->wroff;
		rbytes = len;

		do {
			if (wroff + rbytes > bf->bufsz)
				n = bf->bufsz - wroff;
			else
				n = rbytes;
			/*
			 * Release the nklock while copying the source
			 * data to keep latency low.
			 */
			xnlock_put_irqrestore(&nklock, s);

			ret = xnbufd_copy_to_kmem(bf->bufmem + wroff, bufd, n);
			if (ret < 0)
				return ret;

			xnlock_get_irqsave(&nklock, s);
			/*
			 * In case we were preempted while writing
			 * the message, we have to resend the whole
			 * thing.
			 */
			if (bf->wrtoken != wrtoken) {
				xnbufd_reset(bufd);
				goto redo;
			}

			wroff = (wroff + n) % bf->bufsz;
			rbytes -= n;
		} while (rbytes > 0);

		bf->fillsz += len;
		bf->wroff = wroff;
		ret = (ssize_t)len;

		/*
		 * Wake up all threads pending on the input wait
		 * queue, if we accumulated enough data to feed the
		 * leading one.
		 */
		waiter = xnsynch_peek_pendq(&bf->isynch_base);
		if (waiter && waiter->wait_u.bufd->b_len <= bf->fillsz) {
			if (xnsynch_flush(&bf->isynch_base, 0) == XNSYNCH_RESCHED)
				xnpod_schedule();
		}

		/*
		 * We cannot fail anymore once some data has been
		 * copied via the buffer descriptor, so no need to
		 * check for any reason to invalidate the latter.
		 */
		goto unlock_and_exit;

	wait:
		if (timeout_mode == XN_RELATIVE && timeout == TM_NONBLOCK) {
			ret = -EWOULDBLOCK;
			break;
		}

		if (xnpod_unblockable_p()) {
			ret = -EPERM;
			break;
		}

		thread = xnpod_current_thread();
		thread->wait_u.size = len;
		info = xnsynch_sleep_on(&bf->osynch_base,
					timeout, timeout_mode);
		if (info & XNRMID) {
			ret = -EIDRM;	/* Buffer deleted while pending. */
			break;
		} if (info & XNTIMEO) {
			ret = -ETIMEDOUT;	/* Timeout. */
			break;
		} if (info & XNBREAK) {
			ret = -EINTR;	/* Unblocked. */
			break;
		}
	}

      unlock_and_exit:

	/*
	 * xnpod_schedule() is smarter than us; it will detect any
	 * worthless call inline and won't branch to the rescheduling
	 * code in such a case.
	 */
	xnpod_schedule();

	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

ssize_t rt_buffer_read_inner(RT_BUFFER *bf,
			     struct xnbufd *bufd,
			     xntmode_t timeout_mode, RTIME timeout)
{
	xnthread_t *thread, *waiter;
	size_t len, rbytes, n;
	xnflags_t info;
	u_long rdtoken;
	off_t rdoff;
	ssize_t ret;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	bf = xeno_h2obj_validate(bf, XENO_BUFFER_MAGIC, RT_BUFFER);
	if (bf == NULL) {
		ret = xeno_handle_error(bf, XENO_BUFFER_MAGIC, RT_BUFFER);
		goto unlock_and_exit;
	}

	/*
	 * We may only return complete messages to readers, so there
	 * is no point in waiting for messages which are larger than
	 * what the buffer can hold.
	 */
	len = bufd->b_len;
	if (len > bf->bufsz) {
		ret = -EINVAL;
		goto unlock_and_exit;
	}

	if (len == 0) {
		ret = 0;
		goto unlock_and_exit;
	}

	if (timeout_mode == XN_RELATIVE &&
	    timeout != TM_NONBLOCK && timeout != TM_INFINITE) {
		/*
		 * We may sleep several times before receiving the
		 * data, so let's always use an absolute time spec.
		 */
		timeout_mode = XN_REALTIME;
		timeout += xntbase_get_time(__native_tbase);
	}

redo:
	for (;;) {
		/*
		 * We should be able to read a complete message of the
		 * requested length, or block.
		 */
		if (bf->fillsz < len)
			goto wait;

		/*
		 * Draw the next read token so that we can later
		 * detect preemption.
		 */
		rdtoken = ++bf->rdtoken;

		/* Read from the buffer in a circular way. */
		rdoff = bf->rdoff;
		rbytes = len;

		do {
			if (rdoff + rbytes > bf->bufsz)
				n = bf->bufsz - rdoff;
			else
				n = rbytes;
			/*
			 * Release the nklock while retrieving the
			 * data to keep latency low.
			 */

			xnlock_put_irqrestore(&nklock, s);

			ret = xnbufd_copy_from_kmem(bufd, bf->bufmem + rdoff, n);
			if (ret < 0)
				return ret;

			xnlock_get_irqsave(&nklock, s);
			/*
			 * In case we were preempted while retrieving
			 * the message, we have to re-read the whole
			 * thing.
			 */
			if (bf->rdtoken != rdtoken) {
				xnbufd_reset(bufd);
				goto redo;
			}

			rdoff = (rdoff + n) % bf->bufsz;
			rbytes -= n;
		} while (rbytes > 0);

		bf->fillsz -= len;
		bf->rdoff = rdoff;
		ret = (ssize_t)len;

		/*
		 * Wake up all threads pending on the output wait
		 * queue, if we freed enough room for the leading one
		 * to post its message.
		 */
		waiter = xnsynch_peek_pendq(&bf->osynch_base);
		if (waiter && waiter->wait_u.size + bf->fillsz <= bf->bufsz) {
			if (xnsynch_flush(&bf->osynch_base, 0) == XNSYNCH_RESCHED)
				xnpod_schedule();
		}

		/*
		 * We cannot fail anymore once some data has been
		 * copied via the buffer descriptor, so no need to
		 * check for any reason to invalidate the latter.
		 */
		goto unlock_and_exit;

	wait:
		if (timeout_mode == XN_RELATIVE && timeout == TM_NONBLOCK) {
			ret = -EWOULDBLOCK;
			break;
		}

		if (xnpod_unblockable_p()) {
			ret = -EPERM;
			break;
		}

		/*
		 * Check whether writers are already waiting for
		 * sending data, while we are about to wait for
		 * receiving some. In such a case, we have a
		 * pathological use of the buffer. We must allow for a
		 * short read to prevent a deadlock.
		 */
		if (bf->fillsz > 0 &&
		    xnsynch_nsleepers(&bf->osynch_base) > 0) {
			len = bf->fillsz;
			goto redo;
		}

		thread = xnpod_current_thread();
		thread->wait_u.bufd =  bufd;
		info = xnsynch_sleep_on(&bf->isynch_base,
					timeout, timeout_mode);
		if (info & XNRMID) {
			ret = -EIDRM;	/* Buffer deleted while pending. */
			break;
		} else if (info & XNTIMEO) {
			ret = -ETIMEDOUT;	/* Timeout. */
			break;
		} if (info & XNBREAK) {
			ret = -EINTR;	/* Unblocked. */
			break;
		}
	}

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

/**
 * @fn ssize_t rt_buffer_write(RT_BUFFER *bf, const void *ptr, size_t len, RTIME timeout)
 * @brief Write to a buffer.
 *
 * Writes a message to the specified buffer. If not enough buffer
 * space is available on entry to hold the message, the caller is
 * allowed to block until enough room is freed. Data written by
 * rt_buffer_write() calls can be read in FIFO order by subsequent
 * rt_buffer_read() calls. Messages sent via rt_buffer_write() are
 * handled atomically (no interleave, no short writes).
 *
 * @param bf The descriptor address of the buffer to write to.
 *
 * @param ptr The address of the message data to be written to the
 * buffer.
 *
 * @param len The length in bytes of the message data. Zero is a valid
 * value, in which case the buffer is left untouched, and zero is
 * returned to the caller. No partial message is ever sent.
 *
 * @param timeout The number of clock ticks to wait for enough buffer
 * space to be available to hold the message (see note). Passing
 * TM_INFINITE causes the caller to block indefinitely until enough
 * buffer space is available. Passing TM_NONBLOCK causes the service
 * to return immediately without blocking in case of buffer space
 * shortage.
 *
 * @return The number of bytes written to the buffer is returned upon
 * success. Otherwise:
 *
 * - -ETIMEDOUT is returned if @a timeout is different from
 * TM_NONBLOCK and no buffer space is available within the specified
 * amount of time to hold the message.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to TM_NONBLOCK
 * and no buffer space is immediately available on entry to hold the
 * message.

 * - -EINTR is returned if rt_task_unblock() has been called for the
 * writing task before enough buffer space became available to hold
 * the message.
 *
 * - -EINVAL is returned if @a bf is not a buffer descriptor, or @a
 * len is greater than the actual buffer length.
 *
 * - -EIDRM is returned if @a bf is a deleted buffer descriptor.
 *
 * - -EPERM is returned if this service should block, but was called
 * from a context which cannot sleep (e.g. interrupt, non-realtime
 * context).
 *
 * - -ENOMEM is returned if not enough memory is available from the
 * system heap to hold a temporary copy of the message (user-space
 * call only).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine (non-blocking call only)
 * - Kernel-based task
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always unless the request is immediately satisfied
 * and no task is waiting for messages on the same buffer, or @a
 * timeout specifies a non-blocking operation.
 *
 * @note The @a timeout value will be interpreted as jiffies if the
 * native skin is bound to a periodic time base (see
 * CONFIG_XENO_OPT_NATIVE_PERIOD), or nanoseconds otherwise.
 */

ssize_t rt_buffer_write(RT_BUFFER *bf, const void *ptr, size_t len, RTIME timeout)
{
	struct xnbufd bufd;
	ssize_t ret;

	xnbufd_map_kread(&bufd, ptr, len);
	ret = rt_buffer_write_inner(bf, &bufd, XN_RELATIVE, timeout);
	xnbufd_unmap_kread(&bufd);

	return ret;
}

/**
 * @fn ssize_t rt_buffer_write_until(RT_BUFFER *bf, const void *ptr, size_t len, RTIME timeout)
 * @brief Write to a buffer (with absolute timeout date).
 *
 * Writes a message to the specified buffer. If not enough buffer
 * space is available on entry to hold the message, the caller is
 * allowed to block until enough room is freed, or a timeout elapses.
 *
 * @param bf The descriptor address of the buffer to write to.
 *
 * @param ptr The address of the message data to be written to the
 * buffer.
 *
 * @param len The length in bytes of the message data. Zero is a valid
 * value, in which case the buffer is left untouched, and zero is
 * returned to the caller.
 *
 * @param timeout The absolute date specifying a time limit to wait
 * for enough buffer space to be available to hold the message (see
 * note). Passing TM_INFINITE causes the caller to block indefinitely
 * until enough buffer space is available. Passing TM_NONBLOCK causes
 * the service to return immediately without blocking in case of
 * buffer space shortage.
 *
 * @return The number of bytes written to the buffer is returned upon
 * success. Otherwise:
 *
 * - -ETIMEDOUT is returned if the absolute @a timeout date is reached
 * before enough buffer space is available to hold the message.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to TM_NONBLOCK
 * and no buffer space is immediately available on entry to hold the
 * message.

 * - -EINTR is returned if rt_task_unblock() has been called for the
 * writing task before enough buffer space became available to hold
 * the message.
 *
 * - -EINVAL is returned if @a bf is not a buffer descriptor, or @a
 * len is greater than the actual buffer length.
 *
 * - -EIDRM is returned if @a bf is a deleted buffer descriptor.
 *
 * - -EPERM is returned if this service should block, but was called
 * from a context which cannot sleep (e.g. interrupt, non-realtime
 * context).
 *
 * - -ENOMEM is returned if not enough memory is available from the
 * system heap to hold a temporary copy of the message (user-space
 * call only).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine (non-blocking call only)
 * - Kernel-based task
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always unless the request is immediately satisfied
 * and no task is waiting for messages on the same buffer, or @a
 * timeout specifies a non-blocking operation.
 *
 * @note The @a timeout value will be interpreted as jiffies if the
 * native skin is bound to a periodic time base (see
 * CONFIG_XENO_OPT_NATIVE_PERIOD), or nanoseconds otherwise.
 */

ssize_t rt_buffer_write_until(RT_BUFFER *bf, const void *ptr, size_t len, RTIME timeout)
{

	struct xnbufd bufd;
	ssize_t ret;

	xnbufd_map_kread(&bufd, ptr, len);
	ret = rt_buffer_write_inner(bf, &bufd, XN_REALTIME, timeout);
	xnbufd_unmap_kread(&bufd);

	return ret;
}

/**
 * @fn ssize_t rt_buffer_read(RT_BUFFER *bf, void *ptr, size_t len, RTIME timeout)
 * @brief Read from a buffer.
 *
 * Reads the next message from the specified buffer. If no message is
 * available on entry, the caller is allowed to block until enough
 * data is written to the buffer.
 *
 * @param bf The descriptor address of the buffer to read from.
 *
 * @param ptr A pointer to a memory area which will be written upon
 * success with the received data.
 *
 * @param len The length in bytes of the memory area pointed to by @a
 * ptr. Under normal circumstances, rt_buffer_read() only returns
 * entire messages as specified by the @a len argument, or an error
 * value. However, short reads are allowed when a potential deadlock
 * situation is detected (see note below).
 *
 * @param timeout The number of clock ticks to wait for a message to
 * be available from the buffer (see note). Passing TM_INFINITE causes
 * the caller to block indefinitely until enough data is
 * available. Passing TM_NONBLOCK causes the service to return
 * immediately without blocking in case not enough data is available.
 *
 * @return The number of bytes read from the buffer is returned upon
 * success. Otherwise:
 *
 * - -ETIMEDOUT is returned if @a timeout is different from
 * TM_NONBLOCK and not enough data is available within the specified
 * amount of time to form a complete message.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to TM_NONBLOCK
 * and not enough data is immediately available on entry to form a
 * complete message.

 * - -EINTR is returned if rt_task_unblock() has been called for the
 * reading task before enough data became available to form a complete
 * message.
 *
 * - -EINVAL is returned if @a bf is not a buffer descriptor, or @a
 * len is greater than the actual buffer length.
 *
 * - -EIDRM is returned if @a bf is a deleted buffer descriptor.
 *
 * - -EPERM is returned if this service should block, but was called
 * from a context which cannot sleep (e.g. interrupt, non-realtime
 * context).
 *
 * - -ENOMEM is returned if not enough memory is available from the
 * system heap to hold a temporary copy of the message (user-space
 * call only).
 *
 * @note A short read (i.e. fewer bytes returned than requested by @a
 * len) may happen whenever a pathological use of the buffer is
 * encountered. This condition only arises when the system detects
 * that one or more writers are waiting for sending data, while a
 * reader would have to wait for receiving a complete message at the
 * same time. For instance, consider the following sequence, involving
 * a 1024-byte buffer (bf) and two threads:
 *
 * writer thread > rt_write_buffer(&bf, ptr, 1, TM_INFINITE);
 *        (one byte to read, 1023 bytes available for sending)
 * writer thread > rt_write_buffer(&bf, ptr, 1024, TM_INFINITE);
 *        (writer blocks - no space for another 1024-byte message)
 * reader thread > rt_read_buffer(&bf, ptr, 1024, TM_INFINITE);
 *        (short read - a truncated (1-byte) message is returned)
 *
 * In order to prevent both threads to wait for each other
 * indefinitely, a short read is allowed, which may be completed by a
 * subsequent call to rt_buffer_read() or rt_buffer_read_until().  If
 * that case arises, thread priorities, buffer and/or message lengths
 * should likely be fixed, in order to eliminate such condition.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine (non-blocking call only)
 * - Kernel-based task
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always unless the request is immediately satisfied
 * and no task is waiting for buffer space to be released for the same
 * buffer (see rt_buffer_write()), or @a timeout specifies a
 * non-blocking operation.
 *
 * @note The @a timeout value will be interpreted as jiffies if the
 * native skin is bound to a periodic time base (see
 * CONFIG_XENO_OPT_NATIVE_PERIOD), or nanoseconds otherwise.
 */

ssize_t rt_buffer_read(RT_BUFFER *bf, void *ptr, size_t len, RTIME timeout)
{
	struct xnbufd bufd;
	ssize_t ret;

	xnbufd_map_kwrite(&bufd, ptr, len);
	ret = rt_buffer_read_inner(bf, &bufd, XN_RELATIVE, timeout);
	xnbufd_unmap_kwrite(&bufd);

	return ret;
}

/**
 * @fn ssize_t rt_buffer_read_until(RT_BUFFER *bf, void *ptr, len_t len, RTIME timeout)
 * @brief Read from a buffer (with absolute timeout date).
 *
 * Reads the next message from the specified buffer. If no message is
 * available on entry, the caller is allowed to block until enough
 * data is written to the buffer, or a timeout elapses.
 *
 * @param bf The descriptor address of the buffer to read from.
 *
 * @param ptr A pointer to a memory area which will be written upon
 * success with the received data.
 *
 * @param len The length in bytes of the memory area pointed to by @a
 * ptr. Under normal circumstances, rt_buffer_read_until() only
 * returns entire messages as specified by the @a len argument, or an
 * error value. However, short reads are allowed when a potential
 * deadlock situation is detected (see note below).
 *
 * @param timeout The absolute date specifying a time limit to wait
 * for a message to be available from the buffer (see note). Passing
 * TM_INFINITE causes the caller to block indefinitely until enough
 * data is available. Passing TM_NONBLOCK causes the service to return
 * immediately without blocking in case not enough data is available.
 *
 * @return The number of bytes read from the buffer is returned upon
 * success. Otherwise:
 *
 * - -ETIMEDOUT is returned if the absolute @a timeout date is reached
 * before a complete message arrives.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to TM_NONBLOCK
 * and not enough data is immediately available on entry to form a
 * complete message.

 * - -EINTR is returned if rt_task_unblock() has been called for the
 * reading task before enough data became available to form a complete
 * message.
 *
 * - -EINVAL is returned if @a bf is not a buffer descriptor, or @a
 * len is greater than the actual buffer length.
 *
 * - -EIDRM is returned if @a bf is a deleted buffer descriptor.
 *
 * - -EPERM is returned if this service should block, but was called
 * from a context which cannot sleep (e.g. interrupt, non-realtime
 * context).
 *
 * - -ENOMEM is returned if not enough memory is available from the
 * system heap to hold a temporary copy of the message (user-space
 * call only).
 *
 * @note A short read (i.e. fewer bytes returned than requested by @a
 * len) may happen whenever a pathological use of the buffer is
 * encountered. This condition only arises when the system detects
 * that one or more writers are waiting for sending data, while a
 * reader would have to wait for receiving a complete message at the
 * same time. For instance, consider the following sequence, involving
 * a 1024-byte buffer (bf) and two threads:
 *
 * writer thread > rt_write_buffer(&bf, ptr, 1, TM_INFINITE);
 *        (one byte to read, 1023 bytes available for sending)
 * writer thread > rt_write_buffer(&bf, ptr, 1024, TM_INFINITE);
 *        (writer blocks - no space for another 1024-byte message)
 * reader thread > rt_read_buffer(&bf, ptr, 1024, TM_INFINITE);
 *        (short read - a truncated (1-byte) message is returned)
 *
 * In order to prevent both threads to wait for each other
 * indefinitely, a short read is allowed, which may be completed by a
 * subsequent call to rt_buffer_read() or rt_buffer_read_until().  If
 * that case arises, thread priorities, buffer and/or message lengths
 * should likely be fixed, in order to eliminate such condition.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine (non-blocking call only)
 * - Kernel-based task
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always unless the request is immediately satisfied
 * and no task is waiting for buffer space to be released for the same
 * buffer (see rt_buffer_write()), or @a timeout specifies a
 * non-blocking operation.
 *
 * @note The @a timeout value will be interpreted as jiffies if the
 * native skin is bound to a periodic time base (see
 * CONFIG_XENO_OPT_NATIVE_PERIOD), or nanoseconds otherwise.
 */

ssize_t rt_buffer_read_until(RT_BUFFER *bf, void *ptr, size_t len, RTIME timeout)
{
	struct xnbufd bufd;
	ssize_t ret;

	xnbufd_map_kwrite(&bufd, ptr, len);
	ret = rt_buffer_read_inner(bf, &bufd, XN_REALTIME, timeout);
	xnbufd_unmap_kwrite(&bufd);

	return ret;
}

/**
 * @fn int rt_buffer_clear(RT_BUFFER *bf)
 * @brief Clear a buffer.
 *
 * Empties a buffer from any data.
 *
 * @param bf The descriptor address of the cleared buffer.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a bf is not a buffer descriptor.
 *
 * - -EIDRM is returned if @a bf is a deleted buffer descriptor.
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
 * Rescheduling: possible, as a consequence of resuming tasks that
 * wait for buffer space in rt_buffer_write().
 */

int rt_buffer_clear(RT_BUFFER *bf)
{
	int ret = 0;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	bf = xeno_h2obj_validate(bf, XENO_BUFFER_MAGIC, RT_BUFFER);
	if (bf == NULL) {
		ret = xeno_handle_error(bf, XENO_BUFFER_MAGIC, RT_BUFFER);
		goto unlock_and_exit;
	}

	bf->wroff = 0;
	bf->rdoff = 0;
	bf->fillsz = 0;

	if (xnsynch_flush(&bf->osynch_base, 0) == XNSYNCH_RESCHED)
		xnpod_schedule();

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

/**
 * @fn int rt_buffer_inquire(RT_BUFFER *bf, RT_BUFFER_INFO *info)
 * @brief Inquire about a buffer.
 *
 * Return various information about the status of a given buffer.
 *
 * @param bf The descriptor address of the inquired buffer.
 *
 * @param info The address of a structure the buffer
 * information will be written to.

 * @return 0 is returned and status information is written to the
 * structure pointed at by @a info upon success. Otherwise:
 *
 * - -EINVAL is returned if @a bf is not a buffer
 * descriptor.
 *
 * - -EIDRM is returned if @a bf is a deleted buffer descriptor.
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

int rt_buffer_inquire(RT_BUFFER *bf, RT_BUFFER_INFO *info)
{
	int ret = 0;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	bf = xeno_h2obj_validate(bf, XENO_BUFFER_MAGIC, RT_BUFFER);
	if (bf == NULL) {
		ret = xeno_handle_error(bf, XENO_BUFFER_MAGIC, RT_BUFFER);
		goto unlock_and_exit;
	}

	strcpy(info->name, bf->name);
	info->iwaiters = xnsynch_nsleepers(&bf->isynch_base);
	info->owaiters = xnsynch_nsleepers(&bf->osynch_base);
	info->totalmem = bf->bufsz;
	info->availmem = bf->bufsz - bf->fillsz;

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

/**
 * @fn int rt_buffer_bind(RT_BUFFER *bf, const char *name, RTIME timeout)
 * @brief Bind to a buffer.
 *
 * This user-space only service retrieves the uniform descriptor of a
 * given Xenomai buffer identified by its symbolic name. If the buffer
 * does not exist on entry, this service blocks the caller until a
 * buffer of the given name is created.
 *
 * @param name A valid NULL-terminated name which identifies the
 * buffer to bind to.
 *
 * @param bf The address of a buffer descriptor retrieved by the
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
 * - -EFAULT is returned if @a bf or @a name is referencing invalid
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
 * context).
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
 * CONFIG_XENO_OPT_NATIVE_PERIOD), or nanosebfs otherwise.
 */

/**
 * @fn int rt_buffer_unbind(RT_BUFFER *bf)
 *
 * @brief Unbind from a buffer.
 *
 * This user-space only service unbinds the calling task from the
 * buffer object previously retrieved by a call to rt_buffer_bind().
 *
 * @param bf The address of a buffer descriptor to unbind from.
 *
 * @return 0 is always returned.
 *
 * This service can be called from:
 *
 * - User-space task.
 *
 * Rescheduling: never.
 */

#ifdef CONFIG_XENO_OPT_NATIVE_BUFFER

int __native_buffer_pkg_init(void)
{
	return 0;
}

void __native_buffer_pkg_cleanup(void)
{
	__native_buffer_flush_rq(&__native_global_rholder.bufferq);
}

#endif

/*@}*/

EXPORT_SYMBOL_GPL(rt_buffer_create);
EXPORT_SYMBOL_GPL(rt_buffer_delete);
EXPORT_SYMBOL_GPL(rt_buffer_write);
EXPORT_SYMBOL_GPL(rt_buffer_write_until);
EXPORT_SYMBOL_GPL(rt_buffer_read);
EXPORT_SYMBOL_GPL(rt_buffer_read_until);
EXPORT_SYMBOL_GPL(rt_buffer_inquire);

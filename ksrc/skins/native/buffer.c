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
 *@{*/

#include <nucleus/pod.h>
#include <nucleus/registry.h>
#include <nucleus/heap.h>
#include <native/task.h>
#include <native/buffer.h>
#include <native/timer.h>

#ifdef CONFIG_XENO_EXPORT_REGISTRY

static int __buffer_read_proc(char *page,
			      char **start,
			      off_t off, int count, int *eof, void *data)
{
	RT_BUFFER *bf = (RT_BUFFER *)data;
	char *p = page;
	int len;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	p += sprintf(p, "type=%s:size=%zu:used=%zu\n",
		     bf->mode & B_PRIO ? "PRIO" : "FIFO",
		     bf->bufsz,
		     bf->fillsz);

	if (xnsynch_nsleepers(&bf->isynch_base) > 0) {
		xnpholder_t *holder;

		holder = getheadpq(xnsynch_wait_queue(&bf->osynch_base));

		while (holder) {
			xnthread_t *sleeper = link2thread(holder, plink);
			p += sprintf(p, "+%s (input)\n", xnthread_name(sleeper));
			holder =
			    nextpq(xnsynch_wait_queue(&bf->isynch_base),
				   holder);
		}
	}

	if (xnsynch_nsleepers(&bf->osynch_base) > 0) {
		xnpholder_t *holder;

		holder = getheadpq(xnsynch_wait_queue(&bf->osynch_base));

		while (holder) {
			xnthread_t *sleeper = link2thread(holder, plink);
			p += sprintf(p, "+%s (output)\n", xnthread_name(sleeper));
			holder =
			    nextpq(xnsynch_wait_queue(&bf->osynch_base),
				   holder);
		}
	}

	xnlock_put_irqrestore(&nklock, s);

	len = (p - page) - off;
	if (len <= off + count)
		*eof = 1;
	*start = page + off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;

	return len;
}

extern xnptree_t __native_ptree;

static xnpnode_t __buffer_pnode = {

	.dir = NULL,
	.type = "buffers",
	.entries = 0,
	.read_proc = &__buffer_read_proc,
	.write_proc = NULL,
	.root = &__native_ptree,
};

#elif defined(CONFIG_XENO_OPT_REGISTRY)

static xnpnode_t __buffer_pnode = {

	.type = "buffers"
};

#endif /* CONFIG_XENO_EXPORT_REGISTRY */

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
 * - Kernel-based task
 * - User-space task
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

	bf->bufmem = xnmalloc(bufsz);
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
	bf->rptr = 0;
	bf->wptr = 0;
	bf->fillsz = 0;

#ifdef CONFIG_XENO_OPT_PERVASIVE
	bf->cpid = 0;
#endif /* CONFIG_XENO_OPT_PERVASIVE */
	bf->magic = XENO_BUFFER_MAGIC;

#ifdef CONFIG_XENO_OPT_REGISTRY
	/*
	 * <!> Since xnregister_enter() may reschedule, only register
	 * complete objects, so that the registry cannot return
	 * handles to half-baked objects...
	 */
	if (name) {
		ret = xnregistry_enter(bf->name, bf, &bf->handle,
				       &__buffer_pnode);

		if (ret)
			rt_buffer_delete(bf);
	}
#endif /* CONFIG_XENO_OPT_REGISTRY */

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
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: possible.
 */

int rt_buffer_delete(RT_BUFFER *bf)
{
	int ret = 0, resched;
	spl_t s;

	if (xnpod_asynch_p())
		return -EPERM;

	xnlock_get_irqsave(&nklock, s);

	bf = xeno_h2obj_validate(bf, XENO_BUFFER_MAGIC, RT_BUFFER);
	if (bf == NULL) {
		ret = xeno_handle_error(bf, XENO_BUFFER_MAGIC, RT_BUFFER);
		goto unlock_and_exit;
	}

	xnfree(bf->bufmem);
	removeq(bf->rqueue, &bf->rlink);
	resched = xnsynch_destroy(&bf->isynch_base) == XNSYNCH_RESCHED;
	resched += xnsynch_destroy(&bf->osynch_base) == XNSYNCH_RESCHED;
#ifdef CONFIG_XENO_OPT_REGISTRY
	if (bf->handle)
		xnregistry_remove(bf->handle);
#endif /* CONFIG_XENO_OPT_REGISTRY */

	xeno_mark_deleted(bf);

	if (resched)
		/*
		 * Some task has been woken up as a result of the
		 * deletion: reschedule now.
		 */
		xnpod_schedule();

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

ssize_t rt_buffer_write_inner(RT_BUFFER *bf,
			      const void *ptr, size_t size,
			      xntmode_t timeout_mode, RTIME timeout)
{
	xnthread_t *thread, *waiter;
	size_t rbytes, n, nsum;
	int resched = 0;
	ssize_t ret = 0;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	bf = xeno_h2obj_validate(bf, XENO_BUFFER_MAGIC, RT_BUFFER);
	if (bf == NULL) {
		ret = xeno_handle_error(bf, XENO_BUFFER_MAGIC, RT_BUFFER);
		goto unlock_and_exit;
	}

	if (size > bf->bufsz) {
		ret = -EINVAL;
		goto unlock_and_exit;
	}

	if (size == 0)
		goto unlock_and_exit;

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

	rbytes = size;

	/*
	 * Let's optimize the case where the buffer is empty on entry,
	 * and the leading task blocked on the input queue could be
	 * satisfied by the incoming message; in such a case, we
	 * directly copy the incoming data to the destination memory,
	 * instead of having it transit through the buffer. Otherwise,
	 * we simply accumulate the data into the buffer.
	 */

	if (bf->fillsz > 0)
		/* Buffer has to be empty to keep FIFO ordering. */
		goto accumulate;

	waiter = xnsynch_peek_pendq(&bf->isynch_base);
	if (waiter == NULL)
		/* No blocked task. */
		goto accumulate;

	n = waiter->wait_u.buffer.size;
	if (n > size)
		/* Not enough data to satisfy the request. */
		goto accumulate;

	/* Ok, transfer the message directly, and wake up the task. */
	memcpy(waiter->wait_u.buffer.ptr, ptr, n);
	waiter->wait_u.buffer.size = 0; /* Flags a direct transfer. */
	xnsynch_wakeup_one_sleeper(&bf->isynch_base);

	if (n == size) {
		/* Full message consumed - reschedule and exit. */
		xnpod_schedule();
		goto unlock_and_exit;
	}

	/* Some bytes were not consumed, move them to the buffer. */
	rbytes -= n;
	resched = 1;	/* Rescheduling is pending. */

accumulate:

	for (;;) {
		/* We should be able to copy the entire message, or block. */
		if (bf->fillsz + size <= bf->bufsz) {
			nsum = 0;
			do {
				if (bf->wptr + rbytes > bf->bufsz)
					n = bf->bufsz - bf->wptr;
				else
					n = rbytes;
				memcpy((caddr_t)bf->bufmem + bf->wptr,
				       (caddr_t)ptr + nsum, n);
				bf->wptr = (bf->wptr + n) % bf->bufsz;
				nsum += n;
				rbytes -= n;
			} while (rbytes > 0);
			bf->fillsz += size;
			ret = size;

			/*
			 * Wake up all threads pending on the input
			 * wait queue, if we accumulated enough data
			 * to feed the leading one.
			 */
			waiter = xnsynch_peek_pendq(&bf->isynch_base);
			if (waiter && waiter->wait_u.buffer.size <= bf->fillsz) {
				if (xnsynch_flush(&bf->isynch_base, 0) == XNSYNCH_RESCHED) {
					xnpod_schedule();
					resched = 0;
				}
			}
			break;
		}

		if (timeout_mode == XN_RELATIVE && timeout == TM_NONBLOCK) {
			ret = -EWOULDBLOCK;
			break;
		}

		if (xnpod_unblockable_p()) {
			ret = -EPERM;
			break;
		}

		thread = xnpod_current_thread();
		thread->wait_u.buffer.size = size;
		xnsynch_sleep_on(&bf->osynch_base, timeout, timeout_mode);

		if (xnthread_test_info(thread, XNRMID)) {
			ret = -EIDRM;	/* Buffer deleted while pending. */
			break;
		}
		if (xnthread_test_info(thread, XNTIMEO)) {
			ret = -ETIMEDOUT;	/* Timeout. */
			break;
		}
		if (xnthread_test_info(thread, XNBREAK)) {
			ret = -EINTR;	/* Unblocked. */
			break;
		}
		resched = 0;
	}

      unlock_and_exit:

	if (resched)
		xnpod_schedule();

	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

ssize_t rt_buffer_read_inner(RT_BUFFER *bf,
			     void *ptr, size_t size,
			     xntmode_t timeout_mode, RTIME timeout)
{
	xnthread_t *thread, *waiter;
	size_t rbytes, n, nsum;
	ssize_t ret = 0;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	bf = xeno_h2obj_validate(bf, XENO_BUFFER_MAGIC, RT_BUFFER);
	if (bf == NULL) {
		ret = xeno_handle_error(bf, XENO_BUFFER_MAGIC, RT_BUFFER);
		goto unlock_and_exit;
	}

	if (size > bf->bufsz) {
		ret = -EINVAL;
		goto unlock_and_exit;
	}

	if (size == 0)
		goto unlock_and_exit;

	if (timeout_mode == XN_RELATIVE &&
	    timeout != TM_NONBLOCK && timeout != TM_INFINITE) {
		/*
		 * We may sleep several times before receiving the
		 * data, so let's always use an absolute time spec.
		 */
		timeout_mode = XN_REALTIME;
		timeout += xntbase_get_time(__native_tbase);
	}

	for (;;) {
		/*
		 * We should be able to read a complete message of the
		 * requested size, or block.
		 */
		if (bf->fillsz >= size) {
			rbytes = size;
			nsum = 0;
			do {
				if (bf->rptr + rbytes > bf->bufsz)
					n = bf->bufsz - bf->rptr;
				else
					n = rbytes;
				memcpy((caddr_t)ptr + nsum,
				       (caddr_t)bf->bufmem + bf->rptr, n);
				bf->rptr = (bf->rptr + n) % bf->bufsz;
				nsum += n;
				rbytes -= n;
			} while (rbytes > 0);

			bf->fillsz -= size;
			ret = size;

			/*
			 * Wake up all threads pending on the output
			 * wait queue, if we freed enough room for the
			 * leading one to post its message.
			 */
			waiter = xnsynch_peek_pendq(&bf->osynch_base);
			if (waiter && waiter->wait_u.buffer.size + bf->fillsz <= bf->bufsz) {
				if (xnsynch_flush(&bf->osynch_base, 0) == XNSYNCH_RESCHED)
					xnpod_schedule();
			}
			break;
		}

		if (timeout_mode == XN_RELATIVE && timeout == TM_NONBLOCK) {
			ret = -EWOULDBLOCK;
			break;
		}

		if (xnpod_unblockable_p()) {
			ret = -EPERM;
			break;
		}

		thread = xnpod_current_thread();
		thread->wait_u.buffer.size = size;
		xnsynch_sleep_on(&bf->isynch_base, timeout, timeout_mode);

		if (xnthread_test_info(thread, XNRMID)) {
			ret = -EIDRM;	/* Buffer deleted while pending. */
			break;
		}
		if (xnthread_test_info(thread, XNTIMEO)) {
			ret = -ETIMEDOUT;	/* Timeout. */
			break;
		}
		if (xnthread_test_info(thread, XNBREAK)) {
			ret = -EINTR;	/* Unblocked. */
			break;
		}
		if (thread->wait_u.buffer.size == 0) {
			/* Direct transfer tool place. */
			ret = size;
			break;
		}
	}

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

/**
 * @fn int rt_buffer_write(RT_BUFFER *bf, const void *ptr, size_t size, RTIME timeout)
 * @brief Write to a buffer.
 *
 * Writes a message to the specified buffer. If not enough buffer
 * space is available on entry to hold the message, the caller is
 * allowed to block until enough room is freed. Data written by
 * rt_buffer_write() calls can be read in FIFO order by subsequent
 * rt_buffer_read() calls.
 *
 * @param bf The descriptor address of the buffer to write to.
 *
 * @param ptr The address of the message data to be written to the
 * buffer.
 *
 * @param size The size in bytes of the message data. Zero is a valid
 * value, in which case the buffer is left untouched, and zero is
 * returned to the caller.
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
 * size is greater than the actual buffer size.
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

ssize_t rt_buffer_write(RT_BUFFER *bf, const void *ptr, size_t size, RTIME timeout)
{
	return rt_buffer_write_inner(bf, ptr, size, XN_RELATIVE, timeout);
}

/**
 * @fn int rt_buffer_write_until(RT_BUFFER *bf, const void *ptr, size_t size, RTIME timeout)
 * @brief Write to a buffer (with absolute timeout date).
 *
 * Writes a message to the specified buffer. If not enough buffer
 * space is available on entry to hold the message, the caller is
 * allowed to block until enough room is freed.
 *
 * @param bf The descriptor address of the buffer to write to.
 *
 * @param ptr The address of the message data to be written to the
 * buffer.
 *
 * @param size The size in bytes of the message data. Zero is a valid
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
 * size is greater than the actual buffer size.
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

ssize_t rt_buffer_write_until(RT_BUFFER *bf, const void *ptr, size_t size, RTIME timeout)
{
	return rt_buffer_write_inner(bf, ptr, size, XN_REALTIME, timeout);
}

/**
 * @fn int rt_buffer_read(RT_BUFFER *bf, void *ptr, size_t size, RTIME timeout)
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
 * @param size The length in bytes of the memory area pointed to by @a
 * ptr. rt_buffer_read() only returns entire messages as specified by
 * the @a size argument, or an error value. No partial message is ever
 * returned.
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
 * size is greater than the actual buffer size.
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
 * and no task is waiting for buffer space to be released for the same
 * buffer (see rt_buffer_write()), or @a timeout specifies a
 * non-blocking operation.
 *
 * @note The @a timeout value will be interpreted as jiffies if the
 * native skin is bound to a periodic time base (see
 * CONFIG_XENO_OPT_NATIVE_PERIOD), or nanoseconds otherwise.
 */

ssize_t rt_buffer_read(RT_BUFFER *bf, void *ptr, size_t size, RTIME timeout)
{
	return rt_buffer_read_inner(bf, ptr, size, XN_RELATIVE, timeout);
}

/**
 * @fn int rt_buffer_read_until(RT_BUFFER *bf, void *ptr, size_t size, RTIME timeout)
 * @brief Read from a buffer (with absolute timeout date).
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
 * @param size The length in bytes of the memory area pointed to by @a
 * ptr. rt_buffer_read() only returns entire messages as specified by
 * the @a size argument, or an error value. No partial message is ever
 * returned.
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
 * size is greater than the actual buffer size.
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
 * and no task is waiting for buffer space to be released for the same
 * buffer (see rt_buffer_write()), or @a timeout specifies a
 * non-blocking operation.
 *
 * @note The @a timeout value will be interpreted as jiffies if the
 * native skin is bound to a periodic time base (see
 * CONFIG_XENO_OPT_NATIVE_PERIOD), or nanoseconds otherwise.
 */

ssize_t rt_buffer_read_until(RT_BUFFER *bf, void *ptr, size_t size, RTIME timeout)
{
	return rt_buffer_read_inner(bf, ptr, size, XN_REALTIME, timeout);
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

	bf->wptr = 0;
	bf->rptr = 0;
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

EXPORT_SYMBOL(rt_buffer_create);
EXPORT_SYMBOL(rt_buffer_delete);
EXPORT_SYMBOL(rt_buffer_write);
EXPORT_SYMBOL(rt_buffer_write_until);
EXPORT_SYMBOL(rt_buffer_read);
EXPORT_SYMBOL(rt_buffer_read_until);
EXPORT_SYMBOL(rt_buffer_inquire);

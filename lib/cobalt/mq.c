/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <mqueue.h>
#include <asm/xenomai/syscall.h>
#include "internal.h"

/**
 * @ingroup cobalt
 * @defgroup cobalt_mq Message queues
 *
 * Cobalt/POSIX message queue services
 *
 * A message queue allow exchanging data between real-time
 * threads. For a POSIX message queue, maximum message length and
 * maximum number of messages are fixed when it is created with
 * mq_open().
 *
 *@{
 */

/**
 * @brief Open a message queue
 *
 * This service establishes a connection between the message queue named @a name
 * and the calling context (kernel-space as a whole, or user-space process).
 *
 * One of the following values should be set in @a oflags:
 * - O_RDONLY, meaning that the returned queue descriptor may only be used for
 *   receiving messages;
 * - O_WRONLY, meaning that the returned queue descriptor may only be used for
 *   sending messages;
 * - O_RDWR, meaning that the returned queue descriptor may be used for both
 *   sending and receiving messages.
 *
 * If no message queue named @a name exists, and @a oflags has the @a O_CREAT
 * bit set, the message queue is created by this function, taking two more
 * arguments:
 * - a @a mode argument, of type @b mode_t, currently ignored;
 * - an @a attr argument, pointer to an @b mq_attr structure, specifying the
 *   attributes of the new message queue.
 *
 * If @a oflags has the two bits @a O_CREAT and @a O_EXCL set and the message
 * queue alread exists, this service fails.
 *
 * If the O_NONBLOCK bit is set in @a oflags, the mq_send(), mq_receive(),
 * mq_timedsend() and mq_timedreceive() services return @a -1 with @a errno set
 * to EAGAIN instead of blocking their caller.
 *
 * The following arguments of the @b mq_attr structure at the address @a attr
 * are used when creating a message queue:
 * - @a mq_maxmsg is the maximum number of messages in the queue (128 by
 *   default);
 * - @a mq_msgsize is the maximum size of each message (128 by default).
 *
 * @a name may be any arbitrary string, in which slashes have no particular
 * meaning. However, for portability, using a name which starts with a slash and
 * contains no other slash is recommended.
 *
 * @param name name of the message queue to open;
 *
 * @param oflags flags.
 *
 * @return a message queue descriptor on success;
 * @return -1 with @a errno set if:
 * - ENAMETOOLONG, the length of the @a name argument exceeds 64 characters;
 * - EEXIST, the bits @a O_CREAT and @a O_EXCL were set in @a oflags and the
 *   message queue already exists;
 * - ENOENT, the bit @a O_CREAT is not set in @a oflags and the message queue
 *   does not exist;
 * - ENOSPC, allocation of system memory failed, or insufficient memory exists
 *   in the system heap to create the queue, try increasing
 *   CONFIG_XENO_OPT_SYS_HEAPSZ;
 * - EPERM, attempting to create a message queue from an invalid context;
 * - EINVAL, the @a attr argument is invalid;
 * - EMFILE, too many descriptors are currently open.
 *
 * @par Valid contexts:
 * When creating a message queue, only the following contexts are valid:
 * - kernel module initialization or cleanup routine;
 * - user-space thread (Xenomai threads switch to secondary mode).
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/mq_open.html">
 * Specification.</a>
 */
COBALT_IMPL(mqd_t, mq_open, (const char *name, int oflags, ...))
{
	struct mq_attr *attr = NULL;
	mode_t mode = 0;
	va_list ap;
	int q, err;

	if ((oflags & O_CREAT) != 0) {
		va_start(ap, oflags);
		mode = va_arg(ap, int);	/* unused */
		attr = va_arg(ap, struct mq_attr *);
		va_end(ap);
	}

	q = __STD(open("/dev/null", O_RDWR, 0));
	if (q == -1)
		return (mqd_t) - 1;

	err = -XENOMAI_SKINCALL5(__cobalt_muxid,
				 sc_cobalt_mq_open, name, oflags, mode, attr, q);

	if (!err)
		return (mqd_t) q;

	errno = err;
	return (mqd_t) - 1;
}

/**
 * @brief Close a message queue
 *
 * This service closes the message queue descriptor @a mqd. The
 * message queue is destroyed only when all open descriptors are
 * closed, and when unlinked with a call to the mq_unlink() service.
 *
 * @param mqd message queue descriptor.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EBADF, @a mqd is an invalid message queue descriptor;
 * - EPERM, the caller context is invalid.
 *
 * @par Valid contexts:
 * - kernel module initialization or cleanup routine;
 * - kernel-space cancellation cleanup routine;
 * - user-space thread (Xenomai threads switch to secondary mode);
 * - user-space cancellation cleanup routine.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/mq_close.html">
 * Specification.</a>
 *
 */
COBALT_IMPL(int, mq_close, (mqd_t mqd))
{
	int err;

	err = XENOMAI_SKINCALL1(__cobalt_muxid, sc_cobalt_mq_close, mqd);
	if (!err)
		return __STD(close(mqd));

	errno = -err;
	return -1;
}

/**
 * @brief Unlink a message queue
 *
 * This service unlinks the message queue named @a name. The message queue is
 * not destroyed until all queue descriptors obtained with the mq_open() service
 * are closed with the mq_close() service. However, after a call to this
 * service, the unlinked queue may no longer be reached with the mq_open()
 * service.
 *
 * @param name name of the message queue to be unlinked.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EPERM, the caller context is invalid;
 * - ENAMETOOLONG, the length of the @a name argument exceeds 64 characters;
 * - ENOENT, the message queue does not exist.
 *
 * @par Valid contexts:
 * - kernel module initialization or cleanup routine;
 * - kernel-space cancellation cleanup routine;
 * - user-space thread (Xenomai threads switch to secondary mode);
 * - user-space cancellation cleanup routine.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/mq_unlink.html">
 * Specification.</a>
 *
 */
COBALT_IMPL(int, mq_unlink, (const char *name))
{
	int err;

	err = XENOMAI_SKINCALL1(__cobalt_muxid, sc_cobalt_mq_unlink, name);
	if (!err)
		return 0;

	errno = -err;
	return -1;
}

/**
 * @brief Get message queue attributes
 *
 * This service stores, at the address @a attr, the attributes of the messages
 * queue descriptor @a mqd.
 *
 * The following attributes are set:
 * - @a mq_flags, flags of the message queue descriptor @a mqd;
 * - @a mq_maxmsg, maximum number of messages in the message queue;
 * - @a mq_msgsize, maximum message size;
 * - @a mq_curmsgs, number of messages currently in the queue.
 *
 * @param mqd message queue descriptor;
 *
 * @param attr address where the message queue attributes will be stored on
 * success.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EBADF, @a mqd is not a valid descriptor.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/mq_getattr.html">
 * Specification.</a>
 *
 */
COBALT_IMPL(int, mq_getattr, (mqd_t mqd, struct mq_attr *attr))
{
	int err;

	err = XENOMAI_SKINCALL2(__cobalt_muxid, sc_cobalt_mq_getattr, mqd, attr);
	if (!err)
		return 0;

	errno = -err;
	return -1;
}

/**
 * @brief Set message queue attributes
 *
 * This service sets the flags of the @a mqd descriptor to the value
 * of the member @a mq_flags of the @b mq_attr structure pointed to by
 * @a attr.
 *
 * The previous value of the message queue attributes are stored at the address
 * @a oattr if it is not @a NULL.
 *
 * Only setting or clearing the O_NONBLOCK flag has an effect.
 *
 * @param mqd message queue descriptor;
 *
 * @param attr pointer to new attributes (only @a mq_flags is used);
 *
 * @param oattr if not @a NULL, address where previous message queue attributes
 * will be stored on success.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EBADF, @a mqd is not a valid message queue descriptor.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/mq_setattr.html">
 * Specification.</a>
 *
 */
COBALT_IMPL(int, mq_setattr, (mqd_t mqd,
			      const struct mq_attr *__restrict__ attr,
			      struct mq_attr *__restrict__ oattr))
{
	int err;

	err = XENOMAI_SKINCALL3(__cobalt_muxid,
				sc_cobalt_mq_setattr, mqd, attr, oattr);
	if (!err)
		return 0;

	errno = -err;
	return -1;
}

COBALT_IMPL(int, mq_send, (mqd_t q, const char *buffer, size_t len, unsigned prio))
{
	int err, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	err = XENOMAI_SKINCALL5(__cobalt_muxid,
				sc_cobalt_mq_timedsend, 
				q, buffer, len, prio, NULL);

	pthread_setcanceltype(oldtype, NULL);

	if (!err)
		return 0;

	errno = -err;
	return -1;
}

COBALT_IMPL(int, mq_timedsend, (mqd_t q,
				const char *buffer,
				size_t len,
				unsigned prio, const struct timespec *timeout))
{
	int err, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	err = XENOMAI_SKINCALL5(__cobalt_muxid,
				sc_cobalt_mq_timedsend,
				q, buffer, len, prio, timeout);

	pthread_setcanceltype(oldtype, NULL);

	if (!err)
		return 0;

	errno = -err;
	return -1;
}

COBALT_IMPL(ssize_t, mq_receive, (mqd_t q, char *buffer, size_t len, unsigned *prio))
{
	ssize_t rlen = (ssize_t) len;
	int err, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	err = XENOMAI_SKINCALL5(__cobalt_muxid,
				sc_cobalt_mq_timedreceive, 
				q, buffer, &rlen, prio, NULL);

	pthread_setcanceltype(oldtype, NULL);

	if (!err)
		return rlen;

	errno = -err;
	return -1;
}

COBALT_IMPL(ssize_t, mq_timedreceive, (mqd_t q,
				       char *__restrict__ buffer,
				       size_t len,
				       unsigned *__restrict__ prio,
				       const struct timespec * __restrict__ timeout))
{
	ssize_t rlen = (ssize_t) len;
	int err, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	err = XENOMAI_SKINCALL5(__cobalt_muxid,
				sc_cobalt_mq_timedreceive,
				q, buffer, &rlen, prio, timeout);

	pthread_setcanceltype(oldtype, NULL);

	if (!err)
		return rlen;

	errno = -err;
	return -1;
}

/**
 * @brief Enable notification on message arrival
 *
 * If @a evp is not @a NULL and is the address of a @b sigevent
 * structure with the @a sigev_notify member set to SIGEV_SIGNAL, the
 * current thread will be notified by a signal when a message is sent
 * to the message queue @a mqd, the queue is empty, and no thread is
 * blocked in call to mq_receive() or mq_timedreceive(). After the
 * notification, the thread is unregistered.
 *
 * If @a evp is @a NULL or the @a sigev_notify member is SIGEV_NONE, the current
 * thread is unregistered.
 *
 * Only one thread may be registered at a time.
 *
 * If the current thread is not a Cobalt thread (created with
 * pthread_create()), this service fails.
 *
 * Note that signals sent to user-space Cobalt threads will cause
 * them to switch to secondary mode.
 *
 * @param mqd message queue descriptor;
 *
 * @param evp pointer to an event notification structure.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, @a evp is invalid;
 * - EPERM, the caller context is invalid;
 * - EBADF, @a mqd is not a valid message queue descriptor;
 * - EBUSY, another thread is already registered.
 *
 * @par Valid contexts:
 * - Xenomai kernel-space Cobalt thread,
 * - Xenomai user-space Cobalt thread (switches to primary mode).
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/mq_notify.html">
 * Specification.</a>
 *
 */
COBALT_IMPL(int, mq_notify, (mqd_t mqd, const struct sigevent *evp))
{
	int err;

	err = XENOMAI_SKINCALL2(__cobalt_muxid,
				sc_cobalt_mq_notify, mqd, evp);
	if (err) {
		errno = -err;
		return -1;
	}

	return 0;
}

/** @}*/

/**
 * @file
 * Real-Time Driver Model for Xenomai, device operation multiplexing
 *
 * @note Copyright (C) 2005 Jan Kiszka <jan.kiszka@web.de>
 * @note Copyright (C) 2005 Joerg Langenberg <joerg.langenberg@gmx.net>
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

/*!
 * @ingroup driverapi
 * @defgroup interdrv Inter-Driver API
 * @{
 */

#include <linux/workqueue.h>
#include <cobalt/kernel/ppd.h>
#include <cobalt/kernel/heap.h>
#include <cobalt/kernel/apc.h>
#include "rtdm/syscall.h"
#include "rtdm/internal.h"
#define CREATE_TRACE_POINTS
#include <trace/events/cobalt-rtdm.h>

#define CLOSURE_RETRY_PERIOD_MS	100

#define FD_BITMAP_SIZE  ((RTDM_FD_MAX + BITS_PER_LONG-1) / BITS_PER_LONG)
static unsigned long used_fildes[FD_BITMAP_SIZE];
int open_fildes;       /* number of used descriptors */

DEFINE_XNLOCK(rt_fildes_lock);

static void cleanup_instance(struct rtdm_device *device,
			     struct rtdm_dev_context *context)
{
	if (context) {
		if (device->reserved.exclusive_context)
			context->device = NULL;
		else
			kfree(context);
	}

	rtdm_dereference_device(device);
}

void __rt_dev_close(struct rtdm_fd *fd)
{
	struct rtdm_dev_context *context = rtdm_fd_to_context(fd);
	context->reserved.close(fd);
	cleanup_instance(context->device, context);
}

void __rt_dev_unref(struct rtdm_fd *fd, unsigned int idx)
{
	if (fd->magic != RTDM_FD_MAGIC)
		return;

	xnlock_get(&rt_fildes_lock);
	if (rtdm_fd_owner(fd) == &__xnsys_global_ppd) {
		clear_bit(idx, used_fildes);
		--open_fildes;
	}
	xnlock_put(&rt_fildes_lock);
}

static int create_instance(struct xnsys_ppd *p, int fd,
			struct rtdm_device *device,
			struct rtdm_dev_context **context_ptr)
{
	struct rtdm_dev_context *context;
	spl_t s;
	int err;

	/*
	 * Reset to NULL so that we can always use cleanup_files/instance to
	 * revert also partially successful allocations.
	 */
	*context_ptr = NULL;

	if (p == &__xnsys_global_ppd) {
		xnlock_get_irqsave(&rt_fildes_lock, s);

		if (unlikely(open_fildes >= RTDM_FD_MAX)) {
			xnlock_put_irqrestore(&rt_fildes_lock, s);
			return -ENFILE;
		}

		fd = find_first_zero_bit(used_fildes, RTDM_FD_MAX);
		__set_bit(fd, used_fildes);
		open_fildes++;

		xnlock_put_irqrestore(&rt_fildes_lock, s);
	}

	context = device->reserved.exclusive_context;
	if (context) {
		xnlock_get_irqsave(&rt_dev_lock, s);

		if (unlikely(context->device != NULL)) {
			xnlock_put_irqrestore(&rt_dev_lock, s);
			return -EBUSY;
		}

		context->device = device;

		xnlock_put_irqrestore(&rt_dev_lock, s);
	} else {
		context = kmalloc(sizeof(struct rtdm_dev_context) +
				device->context_size, GFP_KERNEL);
		if (unlikely(context == NULL))
			return -ENOMEM;

		context->device = device;
	}

	context->reserved.close = device->reserved.close;
	if (p == &__xnsys_global_ppd)
		context->reserved.owner = NULL;
	else
		context->reserved.owner = xnshadow_get_context(__rtdm_muxid);

	err = rtdm_fd_enter(p, &context->fd, fd, RTDM_FD_MAGIC, &device->ops);
	if (err < 0)
		return err;

	*context_ptr = context;

	return fd;
}

int __rt_dev_open(struct xnsys_ppd *p, int ufd, const char *path, int oflag)
{
	struct rtdm_device *device;
	struct rtdm_dev_context *context;
	int ret;

	device = get_named_device(path);
	ret = -ENODEV;
	if (!device)
		goto err_out;

	ret = create_instance(p, ufd, device, &context);
	if (ret < 0)
		goto cleanup_out;
	ufd = ret;

	trace_cobalt_fd_open(current, &context->fd, ufd, oflag);

	ret = device->open(&context->fd, oflag);

	if (!XENO_ASSERT(RTDM, !spltest()))
		splnone();

	if (unlikely(ret < 0))
		goto cleanup_out;

	trace_cobalt_fd_created(&context->fd, ufd);

	return ufd;

cleanup_out:
	cleanup_instance(device, context);

err_out:
	return ret;
}
EXPORT_SYMBOL_GPL(__rt_dev_open);

int __rt_dev_socket(struct xnsys_ppd *p, int ufd, int protocol_family,
		    int socket_type, int protocol)
{
	struct rtdm_device *device;
	struct rtdm_dev_context *context;
	int ret;

	device = get_protocol_device(protocol_family, socket_type);
	ret = -EAFNOSUPPORT;
	if (!device)
		goto err_out;

	ret = create_instance(p, ufd, device, &context);
	if (ret < 0)
		goto cleanup_out;
	ufd = ret;

	trace_cobalt_fd_socket(current, &context->fd, ufd, protocol_family);

	ret = device->socket(&context->fd, protocol);

	if (!XENO_ASSERT(RTDM, !spltest()))
		splnone();

	if (unlikely(ret < 0))
		goto cleanup_out;

	trace_cobalt_fd_created(&context->fd, ufd);

	return ufd;

cleanup_out:
	cleanup_instance(device, context);

err_out:
	return ret;
}
EXPORT_SYMBOL_GPL(__rt_dev_socket);

int
__rt_dev_ioctl_fallback(struct rtdm_fd *fd, unsigned int request, void __user *arg)
{
	struct rtdm_device *dev = rtdm_fd_device(fd);
	struct rtdm_device_info dev_info;

	if (fd->magic != RTDM_FD_MAGIC || request != RTIOC_DEVICE_INFO)
		return -ENOSYS;

	dev_info.device_flags = dev->device_flags;
	dev_info.device_class = dev->device_class;
	dev_info.device_sub_class = dev->device_sub_class;
	dev_info.profile_version = dev->profile_version;

	return rtdm_safe_copy_to_user(fd, arg, &dev_info,  sizeof(dev_info));
}

#ifdef DOXYGEN_CPP /* Only used for doxygen doc generation */

/**
 * @brief Increment context reference counter
 *
 * @param[in] context Device context
 *
 * @note rtdm_context_get() automatically increments the lock counter. You
 * only need to call this function in special scenarios, e.g. when keeping
 * additional references to the context structure that have different
 * lifetimes. Only use rtdm_context_lock() on contexts that are currently
 * locked via an earlier rtdm_context_get()/rtdm_contex_lock() or while
 * running a device operation handler.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never.
 */
void rtdm_context_lock(struct rtdm_dev_context *context);

/**
 * @brief Decrement context reference counter
 *
 * @param[in] context Device context
 *
 * @note Every call to rtdm_context_locked() must be matched by a
 * rtdm_context_unlock() invocation.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never.
 */
void rtdm_context_unlock(struct rtdm_dev_context *context);

/**
 * @brief Release a device context obtained via rtdm_context_get()
 *
 * @param[in] context Device context
 *
 * @note Every successful call to rtdm_context_get() must be matched by a
 * rtdm_context_put() invocation.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never.
 */
void rtdm_context_put(struct rtdm_dev_context *context);

/**
 * @brief Open a device
 *
 * Refer to rt_dev_open() for parameters and return values
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 */
int rtdm_open(const char *path, int oflag, ...);

/**
 * @brief Create a socket
 *
 * Refer to rt_dev_socket() for parameters and return values
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 */
int rtdm_socket(int protocol_family, int socket_type, int protocol);

/**
 * @brief Close a device or socket
 *
 * Refer to rt_dev_close() for parameters and return values
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 */
int rtdm_close(int fd);

/**
 * @brief Issue an IOCTL
 *
 * Refer to rt_dev_ioctl() for parameters and return values
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 */
int rtdm_ioctl(int fd, int request, ...);

/**
 * @brief Read from device
 *
 * Refer to rt_dev_read() for parameters and return values
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 */
ssize_t rtdm_read(int fd, void *buf, size_t nbyte);

/**
 * @brief Write to device
 *
 * Refer to rt_dev_write() for parameters and return values
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 */
ssize_t rtdm_write(int fd, const void *buf, size_t nbyte);

/**
 * @brief Receive message from socket
 *
 * Refer to rt_dev_recvmsg() for parameters and return values
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 */
ssize_t rtdm_recvmsg(int fd, struct msghdr *msg, int flags);

/**
 * @brief Receive message from socket
 *
 * Refer to rt_dev_recvfrom() for parameters and return values
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 */
ssize_t rtdm_recvfrom(int fd, void *buf, size_t len, int flags,
		      struct sockaddr *from, socklen_t *fromlen);

/**
 * @brief Receive message from socket
 *
 * Refer to rt_dev_recv() for parameters and return values
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 */
ssize_t rtdm_recv(int fd, void *buf, size_t len, int flags);

/**
 * @brief Transmit message to socket
 *
 * Refer to rt_dev_sendmsg() for parameters and return values
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 */
ssize_t rtdm_sendmsg(int fd, const struct msghdr *msg, int flags);

/**
 * @brief Transmit message to socket
 *
 * Refer to rt_dev_sendto() for parameters and return values
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 */
ssize_t rtdm_sendto(int fd, const void *buf, size_t len, int flags,
		    const struct sockaddr *to, socklen_t tolen);

/**
 * @brief Transmit message to socket
 *
 * Refer to rt_dev_send() for parameters and return values
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 */
ssize_t rtdm_send(int fd, const void *buf, size_t len, int flags);

/**
 * @brief Bind to local address
 *
 * Refer to rt_dev_bind() for parameters and return values
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 */
int rtdm_bind(int fd, const struct sockaddr *my_addr, socklen_t addrlen);

/**
 * @brief Connect to remote address
 *
 * Refer to rt_dev_connect() for parameters and return values
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 */
int rtdm_connect(int fd, const struct sockaddr *serv_addr, socklen_t addrlen);

/**
 * @brief Listen for incomming connection requests
 *
 * Refer to rt_dev_listen() for parameters and return values
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 */
int rtdm_listen(int fd, int backlog);

/**
 * @brief Accept a connection requests
 *
 * Refer to rt_dev_accept() for parameters and return values
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 */
int rtdm_accept(int fd, struct sockaddr *addr, socklen_t *addrlen);

/**
 * @brief Shut down parts of a connection
 *
 * Refer to rt_dev_shutdown() for parameters and return values
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 */
int rtdm_shutdown(int fd, int how);

/**
 * @brief Get socket option
 *
 * Refer to rt_dev_getsockopt() for parameters and return values
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 */
int rtdm_getsockopt(int fd, int level, int optname, void *optval,
		    socklen_t *optlen);

/**
 * @brief Set socket option
 *
 * Refer to rt_dev_setsockopt() for parameters and return values
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 */
int rtdm_setsockopt(int fd, int level, int optname, const void *optval,
		    socklen_t optlen);

/**
 * @brief Get local socket address
 *
 * Refer to rt_dev_getsockname() for parameters and return values
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 */
int rtdm_getsockname(int fd, struct sockaddr *name, socklen_t *namelen);

/**
 * @brief Get socket destination address
 *
 * Refer to rt_dev_getpeername() for parameters and return values
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 */
int rtdm_getpeername(int fd, struct sockaddr *name, socklen_t *namelen);

/** @} */

/*!
 * @addtogroup userapi
 * @{
 */

/**
 * @brief Open a device
 *
 * @param[in] path Device name
 * @param[in] oflag Open flags
 * @param ... Further parameters will be ignored.
 *
 * @return Positive file descriptor value on success, otherwise a negative
 * error code.
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 *
 * @see @c open() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 */
int rt_dev_open(const char *path, int oflag, ...);

/**
 * @brief Create a socket
 *
 * @param[in] protocol_family Protocol family (@c PF_xxx)
 * @param[in] socket_type Socket type (@c SOCK_xxx)
 * @param[in] protocol Protocol ID, 0 for default
 *
 * @return Positive file descriptor value on success, otherwise a negative
 * error code.
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 *
 * @see @c socket() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 */
int rt_dev_socket(int protocol_family, int socket_type, int protocol);

/**
 * @brief Close a device or socket
 *
 * @param[in] fd File descriptor as returned by rt_dev_open() or rt_dev_socket()
 *
 * @return 0 on success, otherwise a negative error code.
 *
 * @note If the matching rt_dev_open() or rt_dev_socket() call took place in
 * non-real-time context, rt_dev_close() must be issued within non-real-time
 * as well. Otherwise, the call will fail.
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 *
 * @see @c close() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 */
int rt_dev_close(int fd);

/**
 * @brief Issue an IOCTL
 *
 * @param[in] fd File descriptor as returned by rt_dev_open() or rt_dev_socket()
 * @param[in] request IOCTL code
 * @param ... Optional third argument, depending on IOCTL function
 * (@c void @c * or @c unsigned @c long)
 *
 * @return Positiv value on success, otherwise negative error code
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 *
 * @see @c ioctl() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 */
int rt_dev_ioctl(int fd, int request, ...);

/**
 * @brief Read from device
 *
 * @param[in] fd File descriptor as returned by rt_dev_open()
 * @param[out] buf Input buffer
 * @param[in] nbyte Number of bytes to read
 *
 * @return Number of bytes read, otherwise negative error code
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 *
 * @see @c read() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 */
ssize_t rt_dev_read(int fd, void *buf, size_t nbyte);

/**
 * @brief Write to device
 *
 * @param[in] fd File descriptor as returned by rt_dev_open()
 * @param[in] buf Output buffer
 * @param[in] nbyte Number of bytes to write
 *
 * @return Number of bytes written, otherwise negative error code
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 *
 * @see @c write() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 */
ssize_t rt_dev_write(int fd, const void *buf, size_t nbyte);

/**
 * @brief Receive message from socket
 *
 * @param[in] fd File descriptor as returned by rt_dev_socket()
 * @param[in,out] msg Message descriptor
 * @param[in] flags Message flags
 *
 * @return Number of bytes received, otherwise negative error code
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 *
 * @see @c recvmsg() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 */
ssize_t rt_dev_recvmsg(int fd, struct msghdr *msg, int flags);

/**
 * @brief Receive message from socket
 *
 * @param[in] fd File descriptor as returned by rt_dev_socket()
 * @param[out] buf Message buffer
 * @param[in] len Message buffer size
 * @param[in] flags Message flags
 * @param[out] from Buffer for message sender address
 * @param[in,out] fromlen Address buffer size
 *
 * @return Number of bytes received, otherwise negative error code
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 *
 * @see @c recvfrom() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 */
ssize_t rt_dev_recvfrom(int fd, void *buf, size_t len, int flags,
			struct sockaddr *from, socklen_t *fromlen);

/**
 * @brief Receive message from socket
 *
 * @param[in] fd File descriptor as returned by rt_dev_socket()
 * @param[out] buf Message buffer
 * @param[in] len Message buffer size
 * @param[in] flags Message flags
 *
 * @return Number of bytes received, otherwise negative error code
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 *
 * @see @c recv() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 */
ssize_t rt_dev_recv(int fd, void *buf, size_t len, int flags);

/**
 * @brief Transmit message to socket
 *
 * @param[in] fd File descriptor as returned by rt_dev_socket()
 * @param[in] msg Message descriptor
 * @param[in] flags Message flags
 *
 * @return Number of bytes sent, otherwise negative error code
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 *
 * @see @c sendmsg() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 */
ssize_t rt_dev_sendmsg(int fd, const struct msghdr *msg, int flags);

/**
 * @brief Transmit message to socket
 *
 * @param[in] fd File descriptor as returned by rt_dev_socket()
 * @param[in] buf Message buffer
 * @param[in] len Message buffer size
 * @param[in] flags Message flags
 * @param[in] to Buffer for message destination address
 * @param[in] tolen Address buffer size
 *
 * @return Number of bytes sent, otherwise negative error code
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 *
 * @see @c sendto() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 */
ssize_t rt_dev_sendto(int fd, const void *buf, size_t len, int flags,
		      const struct sockaddr *to, socklen_t tolen);

/**
 * @brief Transmit message to socket
 *
 * @param[in] fd File descriptor as returned by rt_dev_socket()
 * @param[in] buf Message buffer
 * @param[in] len Message buffer size
 * @param[in] flags Message flags
 *
 * @return Number of bytes sent, otherwise negative error code
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 *
 * @see @c send() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 */
ssize_t rt_dev_send(int fd, const void *buf, size_t len, int flags);

/**
 * @brief Bind to local address
 *
 * @param[in] fd File descriptor as returned by rt_dev_socket()
 * @param[in] my_addr Address buffer
 * @param[in] addrlen Address buffer size
 *
 * @return 0 on success, otherwise negative error code
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 *
 * @see @c bind() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 */
int rt_dev_bind(int fd, const struct sockaddr *my_addr, socklen_t addrlen);

/**
 * @brief Connect to remote address
 *
 * @param[in] fd File descriptor as returned by rt_dev_socket()
 * @param[in] serv_addr Address buffer
 * @param[in] addrlen Address buffer size
 *
 * @return 0 on success, otherwise negative error code
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 *
 * @see @c connect() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 */
int rt_dev_connect(int fd, const struct sockaddr *serv_addr,
		   socklen_t addrlen);

/**
 * @brief Listen for incomming connection requests
 *
 * @param[in] fd File descriptor as returned by rt_dev_socket()
 * @param[in] backlog Maximum queue length
 *
 * @return 0 on success, otherwise negative error code
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 *
 * @see @c lsiten() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 */
int rt_dev_listen(int fd, int backlog);

/**
 * @brief Accept a connection requests
 *
 * @param[in] fd File descriptor as returned by rt_dev_socket()
 * @param[out] addr Buffer for remote address
 * @param[in,out] addrlen Address buffer size
 *
 * @return 0 on success, otherwise negative error code
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 *
 * @see @c accept() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 */
int rt_dev_accept(int fd, struct sockaddr *addr, socklen_t *addrlen);

/**
 * @brief Shut down parts of a connection
 *
 * @param[in] fd File descriptor as returned by rt_dev_socket()
 * @param[in] how Specifies the part to be shut down (@c SHUT_xxx)
*
 * @return 0 on success, otherwise negative error code
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 *
 * @see @c shutdown() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 */
int rt_dev_shutdown(int fd, int how);

/**
 * @brief Get socket option
 *
 * @param[in] fd File descriptor as returned by rt_dev_socket()
 * @param[in] level Addressed stack level
 * @param[in] optname Option name ID
 * @param[out] optval Value buffer
 * @param[in,out] optlen Value buffer size
 *
 * @return 0 on success, otherwise negative error code
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 *
 * @see @c getsockopt() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 */
int rt_dev_getsockopt(int fd, int level, int optname, void *optval,
		      socklen_t *optlen);

/**
 * @brief Set socket option
 *
 * @param[in] fd File descriptor as returned by rt_dev_socket()
 * @param[in] level Addressed stack level
 * @param[in] optname Option name ID
 * @param[in] optval Value buffer
 * @param[in] optlen Value buffer size
 *
 * @return 0 on success, otherwise negative error code
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 *
 * @see @c setsockopt() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 */
int rt_dev_setsockopt(int fd, int level, int optname, const void *optval,
		      socklen_t optlen);

/**
 * @brief Get local socket address
 *
 * @param[in] fd File descriptor as returned by rt_dev_socket()
 * @param[out] name Address buffer
 * @param[in,out] namelen Address buffer size
 *
 * @return 0 on success, otherwise negative error code
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 *
 * @see @c getsockname() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 */
int rt_dev_getsockname(int fd, struct sockaddr *name, socklen_t *namelen);

/**
 * @brief Get socket destination address
 *
 * @param[in] fd File descriptor as returned by rt_dev_socket()
 * @param[out] name Address buffer
 * @param[in,out] namelen Address buffer size
 *
 * @return 0 on success, otherwise negative error code
 *
 * Environments:
 *
 * Depends on driver implementation, see @ref profiles "Device Profiles".
 *
 * Rescheduling: possible.
 *
 * @see @c getpeername() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 */
int rt_dev_getpeername(int fd, struct sockaddr *name, socklen_t *namelen);
/** @} */

#endif /* DOXYGEN_CPP */

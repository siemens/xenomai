/*
 * Copyright (C) 2005 Jan Kiszka <jan.kiszka@web.de>
 * Copyright (C) 2005 Joerg Langenberg <joerg.langenberg@gmx.net>
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
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <cobalt/kernel/ppd.h>
#include <cobalt/kernel/heap.h>
#include <cobalt/kernel/apc.h>
#include "rtdm/internal.h"
#define CREATE_TRACE_POINTS
#include <trace/events/cobalt-rtdm.h>

/**
 * @ingroup rtdm
 * @defgroup rtdm_driver_interface Driver programming interface
 * RTDM driver programming interface
 * @{
 */

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
	if (context->reserved.close)
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
			err = -EBUSY;
			goto fail;
		}

		context->device = device;

		xnlock_put_irqrestore(&rt_dev_lock, s);
	} else {
		context = kmalloc(sizeof(struct rtdm_dev_context) +
				device->context_size, GFP_KERNEL);
		if (unlikely(context == NULL)) {
			err = -ENOMEM;
			goto fail;
		}

		context->device = device;
	}

	context->reserved.close = device->reserved.close;
	*context_ptr = context;

	err = rtdm_fd_enter(p, &context->fd, fd, RTDM_FD_MAGIC, &device->ops);
	if (err < 0)
		goto fail;

	return fd;
fail:
	if (p == &__xnsys_global_ppd) {
		xnlock_get_irqsave(&rt_fildes_lock, s);
		__clear_bit(fd, used_fildes);
		open_fildes--;
		xnlock_put_irqrestore(&rt_fildes_lock, s);
	}

	return err;
}

int __rt_dev_open(struct xnsys_ppd *p, int ufd, const char *path, int oflag)
{
	struct rtdm_dev_context *context;
	struct rtdm_device *device;
	int ret, minor;

	/* skip common /dev prefix */
	if (strncmp(path, "/dev/", 5) == 0)
		path += 5;

	device = __rtdm_get_named_device(path, &minor);
	if (device == NULL)
		return -ENODEV;

	ret = create_instance(p, ufd, device, &context);
	if (ret < 0)
		goto cleanup_out;

	ufd = ret;
	context->fd.minor = minor;

	trace_cobalt_fd_open(current, &context->fd, ufd, oflag);

	if (device->ops.open) {
		ret = device->ops.open(&context->fd, oflag);
		if (!XENO_ASSERT(COBALT, !spltest()))
			splnone();
		if (ret < 0)
			goto cleanup_out;
	}

	trace_cobalt_fd_created(&context->fd, ufd);

	return ufd;

cleanup_out:
	cleanup_instance(device, context);

	return ret;
}
EXPORT_SYMBOL_GPL(__rt_dev_open);

int __rt_dev_socket(struct xnsys_ppd *p, int ufd, int protocol_family,
		    int socket_type, int protocol)
{
	struct rtdm_device *device;
	struct rtdm_dev_context *context;
	int ret;

	device = __rtdm_get_protocol_device(protocol_family, socket_type);
	if (device == NULL)
		return -EAFNOSUPPORT;

	ret = create_instance(p, ufd, device, &context);
	if (ret < 0)
		goto cleanup_out;
	ufd = ret;

	trace_cobalt_fd_socket(current, &context->fd, ufd, protocol_family);

	if (device->ops.socket) {
		ret = device->ops.socket(&context->fd, protocol);
		if (!XENO_ASSERT(COBALT, !spltest()))
			splnone();
		if (ret < 0)
			goto cleanup_out;
	}

	trace_cobalt_fd_created(&context->fd, ufd);

	return ufd;

cleanup_out:
	cleanup_instance(device, context);

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
 * @addtogroup rtdm_sync
 *@{
 */

/**
 * @fn void rtdm_waitqueue_init(struct rtdm_waitqueue *wq)
 * @brief  Initialize a RTDM wait queue
 *
 * Sets up a wait queue structure for further use.
 *
 * @param wq waitqueue to initialize.
 *
 * @coretags{task-unrestricted}
 */
void rtdm_waitqueue_init(struct rtdm_waitqueue *wq);

/**
 * @fn void rtdm_waitqueue_destroy(struct rtdm_waitqueue *wq)
 * @brief  Deletes a RTDM wait queue
 *
 * Dismantles a wait queue structure, releasing all resources attached
 * to it.
 *
 * @param wq waitqueue to delete.
 *
 * @coretags{task-unrestricted}
 */
void rtdm_waitqueue_destroy(struct rtdm_waitqueue *wq);

/**
 * @fn rtdm_timedwait_condition_locked(struct rtdm_wait_queue *wq, C_expr condition, nanosecs_rel_t timeout, rtdm_toseq_t *toseq)
 * @brief Timed sleep on a locked waitqueue until a condition gets true
 *
 * The calling task is put to sleep until @a condition evaluates to
 * true or a timeout occurs. The condition is checked each time the
 * waitqueue @a wq is signaled.
 *
 * The waitqueue must have been locked by a call to
 * rtdm_waitqueue_lock() prior to calling this service.
 *
 * @param wq locked waitqueue to wait on. The waitqueue lock is
 * dropped when sleeping, then reacquired before this service returns
 * to the caller.
 *
 * @param condition C expression for the event to wait for.
 *
 * @param timeout relative timeout in nanoseconds, see
 * @ref RTDM_TIMEOUT_xxx for special values.
 * 
 * @param[in,out] toseq handle of a timeout sequence as returned by
 * rtdm_toseq_init() or NULL.
 *
 * @return 0 on success, otherwise:
 *
 * - -EINTR is returned if calling task has received a Linux signal or
 * has been forcibly unblocked by a call to rtdm_task_unblock().
 *
 * - -ETIMEDOUT is returned if the if the request has not been satisfied
 * within the specified amount of time.
 *
 * @note rtdm_waitqueue_signal() has to be called after changing any
 * variable that could change the result of the wait condition.
 *
 * @note Passing RTDM_TIMEOUT_NONE to @a timeout makes no sense for
 * such service, and might cause unexpected behavior.
 *
 * @coretags{primary-only, might-switch}
 */
rtdm_timedwait_condition_locked(struct rtdm_wait_queue *wq, C_expr condition,
				nanosecs_rel_t timeout, rtdm_toseq_t *toseq);

/**
 * @fn rtdm_wait_condition_locked(struct rtdm_wait_queue *wq, C_expr condition)
 * @brief Sleep on a locked waitqueue until a condition gets true
 *
 * The calling task is put to sleep until @a condition evaluates to
 * true. The condition is checked each time the waitqueue @a wq is
 * signaled.
 *
 * The waitqueue must have been locked by a call to
 * rtdm_waitqueue_lock() prior to calling this service.
 *
 * @param wq locked waitqueue to wait on. The waitqueue lock is
 * dropped when sleeping, then reacquired before this service returns
 * to the caller.
 *
 * @param condition C expression for the event to wait for.
 *
 * @return 0 on success, otherwise:
 *
 * - -EINTR is returned if calling task has received a Linux signal or
 * has been forcibly unblocked by a call to rtdm_task_unblock().
 *
 * @note rtdm_waitqueue_signal() has to be called after changing any
 * variable that could change the result of the wait condition.
 *
 * @coretags{primary-only, might-switch}
 */
rtdm_wait_condition_locked(struct rtdm_wait_queue *wq, C_expr condition);

/**
 * @fn rtdm_timedwait_condition(struct rtdm_wait_queue *wq, C_expr condition, nanosecs_rel_t timeout, rtdm_toseq_t *toseq)
 * @brief Timed sleep on a waitqueue until a condition gets true
 *
 * The calling task is put to sleep until @a condition evaluates to
 * true or a timeout occurs. The condition is checked each time the
 * waitqueue @a wq is signaled.
 *
 * @param wq waitqueue to wait on.
 *
 * @param condition C expression for the event to wait for.
 *
 * @param timeout relative timeout in nanoseconds, see
 * @ref RTDM_TIMEOUT_xxx for special values.
 * 
 * @param[in,out] toseq handle of a timeout sequence as returned by
 * rtdm_toseq_init() or NULL.
 *
 * @return 0 on success, otherwise:
 *
 * - -EINTR is returned if calling task has received a Linux signal or
 * has been forcibly unblocked by a call to rtdm_task_unblock().
 *
 * - -ETIMEDOUT is returned if the if the request has not been satisfied
 * within the specified amount of time.
 *
 * @note rtdm_waitqueue_signal() has to be called after changing any
 * variable that could change the result of the wait condition.
 *
 * @note Passing RTDM_TIMEOUT_NONE to @a timeout makes no sense for
 * such service, and might cause unexpected behavior.
 *
 * @coretags{primary-only, might-switch}
 */
rtdm_timedwait_condition(struct rtdm_wait_queue *wq, C_expr condition,
			 nanosecs_rel_t timeout, rtdm_toseq_t *toseq);

/**
 * @fn void rtdm_timedwait(struct rtdm_wait_queue *wq, nanosecs_rel_t timeout, rtdm_toseq_t *toseq)
 * @brief Timed sleep on a waitqueue unconditionally
 *
 * The calling task is put to sleep until the waitqueue is signaled by
 * either rtdm_waitqueue_signal() or rtdm_waitqueue_broadcast(), or
 * flushed by a call to rtdm_waitqueue_flush(), or a timeout occurs.
 *
 * @param wq waitqueue to wait on.
 *
 * @param timeout relative timeout in nanoseconds, see
 * @ref RTDM_TIMEOUT_xxx for special values.
 * 
 * @param[in,out] toseq handle of a timeout sequence as returned by
 * rtdm_toseq_init() or NULL.
 *
 * @return 0 on success, otherwise:
 *
 * - -EINTR is returned if the waitqueue has been flushed, or the
 * calling task has received a Linux signal or has been forcibly
 * unblocked by a call to rtdm_task_unblock().
 *
 * - -ETIMEDOUT is returned if the if the request has not been satisfied
 * within the specified amount of time.
 *
 * @note Passing RTDM_TIMEOUT_NONE to @a timeout makes no sense for
 * such service, and might cause unexpected behavior.
 *
 * @coretags{primary-only, might-switch}
 */
void rtdm_timedwait(struct rtdm_wait_queue *wq,
		    nanosecs_rel_t timeout, rtdm_toseq_t *toseq);

/**
 * @fn void rtdm_timedwait_locked(struct rtdm_wait_queue *wq, nanosecs_rel_t timeout, rtdm_toseq_t *toseq)
 * @brief Timed sleep on a locked waitqueue unconditionally
 *
 * The calling task is put to sleep until the waitqueue is signaled by
 * either rtdm_waitqueue_signal() or rtdm_waitqueue_broadcast(), or
 * flushed by a call to rtdm_waitqueue_flush(), or a timeout occurs.
 *
 * The waitqueue must have been locked by a call to
 * rtdm_waitqueue_lock() prior to calling this service.
 *
 * @param wq locked waitqueue to wait on. The waitqueue lock is
 * dropped when sleeping, then reacquired before this service returns
 * to the caller.
 *
 * @param timeout relative timeout in nanoseconds, see
 * @ref RTDM_TIMEOUT_xxx for special values.
 * 
 * @param[in,out] toseq handle of a timeout sequence as returned by
 * rtdm_toseq_init() or NULL.
 *
 * @return 0 on success, otherwise:
 *
 * - -EINTR is returned if the waitqueue has been flushed, or the
 * calling task has received a Linux signal or has been forcibly
 * unblocked by a call to rtdm_task_unblock().
 *
 * - -ETIMEDOUT is returned if the if the request has not been satisfied
 * within the specified amount of time.
 *
 * @note Passing RTDM_TIMEOUT_NONE to @a timeout makes no sense for
 * such service, and might cause unexpected behavior.
 *
 * @coretags{primary-only, might-switch}
 */
void rtdm_timedwait_locked(struct rtdm_wait_queue *wq,
			   nanosecs_rel_t timeout, rtdm_toseq_t *toseq);

/**
 * @fn rtdm_wait_condition(struct rtdm_wait_queue *wq, C_expr condition)
 * @brief Sleep on a waitqueue until a condition gets true
 *
 * The calling task is put to sleep until @a condition evaluates to
 * true. The condition is checked each time the waitqueue @a wq is
 * signaled.
 *
 * @param wq waitqueue to wait on
 *
 * @param condition C expression for the event to wait for.
 *
 * @return 0 on success, otherwise:
 *
 * - -EINTR is returned if calling task has received a Linux signal or
 * has been forcibly unblocked by a call to rtdm_task_unblock().
 *
 * @note rtdm_waitqueue_signal() has to be called after changing any
 * variable that could change the result of the wait condition.
 *
 * @coretags{primary-only, might-switch}
 */
rtdm_wait_condition(struct rtdm_wait_queue *wq, C_expr condition);

/**
 * @fn void rtdm_wait(struct rtdm_wait_queue *wq)
 * @brief Sleep on a waitqueue unconditionally
 *
 * The calling task is put to sleep until the waitqueue is signaled by
 * either rtdm_waitqueue_signal() or rtdm_waitqueue_broadcast(), or
 * flushed by a call to rtdm_waitqueue_flush().
 *
 * @param wq waitqueue to wait on.
 *
 * @return 0 on success, otherwise:
 *
 * - -EINTR is returned if the waitqueue has been flushed, or the
 * calling task has received a Linux signal or has been forcibly
 * unblocked by a call to rtdm_task_unblock().
 *
 * @coretags{primary-only, might-switch}
 */
void rtdm_wait(struct rtdm_wait_queue *wq);

/**
 * @fn void rtdm_wait_locked(struct rtdm_wait_queue *wq)
 * @brief Sleep on a locked waitqueue unconditionally
 *
 * The calling task is put to sleep until the waitqueue is signaled by
 * either rtdm_waitqueue_signal() or rtdm_waitqueue_broadcast(), or
 * flushed by a call to rtdm_waitqueue_flush().
 *
 * The waitqueue must have been locked by a call to
 * rtdm_waitqueue_lock() prior to calling this service.
 *
 * @param wq locked waitqueue to wait on. The waitqueue lock is
 * dropped when sleeping, then reacquired before this service returns
 * to the caller.
 *
 * @return 0 on success, otherwise:
 *
 * - -EINTR is returned if the waitqueue has been flushed, or the
 * calling task has received a Linux signal or has been forcibly
 * unblocked by a call to rtdm_task_unblock().
 *
 * @coretags{primary-only, might-switch}
 */
void rtdm_wait_locked(struct rtdm_wait_queue *wq);

/**
 * @fn void rtdm_waitqueue_lock(struct rtdm_wait_queue *wq, rtdm_lockctx_t context)
 * @brief Lock a waitqueue
 *
 * Acquires the lock on the waitqueue @a wq.
 *
 * @param wq waitqueue to lock.
 *
 * @param context name of local variable to store the context in.
 *
 * @note Recursive locking might lead to unexpected behavior,
 * including lock up.
 *
 * @coretags{unrestricted}
 */
void rtdm_waitqueue_lock(struct rtdm_wait_queue *wq, rtdm_lockctx_t context);

/**
 * @fn void rtdm_waitqueue_unlock(struct rtdm_wait_queue *wq, rtdm_lockctx_t context)
 * @brief Unlock a waitqueue
 *
 * Releases the lock on the waitqueue @a wq.
 *
 * @param wq waitqueue to unlock.
 *
 * @param context name of local variable to retrieve the context from.
 *
 * @coretags{unrestricted}
 */
void rtdm_waitqueue_unlock(struct rtdm_wait_queue *wq, rtdm_lockctx_t context);

/**
 * @fn void rtdm_waitqueue_signal(struct rtdm_wait_queue *wq)
 * @brief Signal a waitqueue
 *
 * Signals the waitqueue @a wq, waking up a single waiter (if
 * any).
 *
 * @param wq waitqueue to signal.
 *
 * @return non-zero if a task has been readied as a result of this
 * call, zero otherwise.
 *
 * @coretags{unrestricted, might-switch}
 */
void rtdm_waitqueue_signal(struct rtdm_wait_queue *wq);

/**
 * @fn void rtdm_waitqueue_broadcast(struct rtdm_wait_queue *wq)
 * @brief Broadcast a waitqueue
 *
 * Broadcast the waitqueue @a wq, waking up all waiters. Each
 * readied task may assume to have received the wake up event.
 *
 * @param wq waitqueue to broadcast.
 *
 * @return non-zero if at least one task has been readied as a result
 * of this call, zero otherwise.
 *
 * @coretags{unrestricted, might-switch}
 */
void rtdm_waitqueue_broadcast(struct rtdm_wait_queue *wq);

/**
 * @fn void rtdm_waitqueue_flush(struct rtdm_wait_queue *wq)
 * @brief Flush a waitqueue
 *
 * Flushes the waitqueue @a wq, unblocking all waiters with an error
 * status (-EINTR).
 *
 * @param wq waitqueue to flush.
 *
 * @return non-zero if at least one task has been readied as a result
 * of this call, zero otherwise.
 *
 * @coretags{unrestricted, might-switch}
 */
void rtdm_waitqueue_flush(struct rtdm_wait_queue *wq);

/**
 * @fn void rtdm_waitqueue_wakeup(struct rtdm_wait_queue *wq, rtdm_task_t waiter)
 * @brief Signal a particular waiter on a waitqueue
 *
 * Signals the waitqueue @a wq, waking up waiter @a waiter only,
 * which must be currently sleeping on the waitqueue.
 *
 * @param wq waitqueue to signal.
 *
 * @param waiter RTDM task to wake up.
 *
 * @coretags{unrestricted, might-switch}
 */
void rtdm_waitqueue_wakeup(struct rtdm_wait_queue *wq, rtdm_task_t waiter);

/**
 * @fn rtdm_for_each_waiter(rtdm_task_t pos, struct rtdm_wait_queue *wq)
 * @brief Simple iterator for waitqueues
 *
 * This construct traverses the wait list of a given waitqueue
 * @a wq, assigning each RTDM task pointer to the cursor variable
 * @a pos, which must be of type rtdm_task_t.
 *
 * @a wq must have been locked by a call to rtdm_waitqueue_lock()
 * prior to traversing its wait list.
 *
 * @param pos cursor variable holding a pointer to the RTDM task
 * being fetched.
 *
 * @param wq waitqueue to scan.
 *
 * @note The waitqueue should not be signaled, broadcast or flushed
 * during the traversal, unless the loop is aborted immediately
 * after. Should multiple waiters be readied while iterating, the safe
 * form rtdm_for_each_waiter_safe() must be used for traversal
 * instead.
 *
 * @coretags{unrestricted}
 */
rtdm_for_each_waiter(rtdm_task_t pos, struct rtdm_wait_queue *wq);

/**
 * @fn rtdm_for_each_waiter_safe(rtdm_task_t pos, rtdm_task_t tmp, struct rtdm_wait_queue *wq)
 * @brief Safe iterator for waitqueues
 *
 * This construct traverses the wait list of a given waitqueue
 * @a wq, assigning each RTDM task pointer to the cursor variable
 * @a pos, which must be of type rtdm_task_t.
 *
 * Unlike with rtdm_for_each_waiter(), the waitqueue may be signaled,
 * broadcast or flushed during the traversal.
 *
 * @a wq must have been locked by a call to rtdm_waitqueue_lock()
 * prior to traversing its wait list.
 *
 * @param pos cursor variable holding a pointer to the RTDM task
 * being fetched.
 *
 * @param tmp temporary cursor variable.
 *
 * @param wq waitqueue to scan.
 *
 * @coretags{unrestricted}
 */
rtdm_for_each_waiter_safe(rtdm_task_t pos, rtdm_task_t tmp, struct rtdm_wait_queue *wq);

/** @} rtdm_sync */

/**
 * @defgroup rtdm_interdriver_api Driver to driver services
 * Inter-driver interface
 *@{
 */

/**
 * @brief Open a device
 *
 * Refer to rt_dev_open() for parameters and return values
 *
 * @coretags{secondary-only, might-switch}
 */
int rtdm_open(const char *path, int oflag, ...);

/**
 * @brief Create a socket
 *
 * Refer to rt_dev_socket() for parameters and return values. Action
 * depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @coretags{secondary-only, might-switch}
 */
int rtdm_socket(int protocol_family, int socket_type, int protocol);

/**
 * @brief Close a device or socket
 *
 * Refer to rt_dev_close() for parameters and return values. Action
 * depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @coretags{secondary-only, might-switch}
 */
int rtdm_close(int fd);

/**
 * @brief Issue an IOCTL
 *
 * Refer to rt_dev_ioctl() for parameters and return values. Action
 * depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @coretags{task-unrestricted, might-switch}
 */
int rtdm_ioctl(int fd, int request, ...);

/**
 * @brief Read from device
 *
 * Refer to rt_dev_read() for parameters and return values. Action
 * depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @coretags{mode-unrestricted, might-switch}
 */
ssize_t rtdm_read(int fd, void *buf, size_t nbyte);

/**
 * @brief Write to device
 *
 * Refer to rt_dev_write() for parameters and return values. Action
 * depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @coretags{mode-unrestricted, might-switch}
 */
ssize_t rtdm_write(int fd, const void *buf, size_t nbyte);

/**
 * @brief Receive message from socket
 *
 * Refer to rt_dev_recvmsg() for parameters and return values. Action
 * depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @coretags{mode-unrestricted, might-switch}
 */
ssize_t rtdm_recvmsg(int fd, struct msghdr *msg, int flags);

/**
 * @brief Receive message from socket
 *
 * Refer to rt_dev_recvfrom() for parameters and return values. Action
 * depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @coretags{mode-unrestricted, might-switch}
 */
ssize_t rtdm_recvfrom(int fd, void *buf, size_t len, int flags,
		      struct sockaddr *from, socklen_t *fromlen);

/**
 * @brief Receive message from socket
 *
 * Refer to rt_dev_recv() for parameters and return values. Action
 * depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @coretags{mode-unrestricted, might-switch}
 */
ssize_t rtdm_recv(int fd, void *buf, size_t len, int flags);

/**
 * @brief Transmit message to socket
 *
 * Refer to rt_dev_sendmsg() for parameters and return values. Action
 * depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @coretags{mode-unrestricted, might-switch}
 */
ssize_t rtdm_sendmsg(int fd, const struct msghdr *msg, int flags);

/**
 * @brief Transmit message to socket
 *
 * Refer to rt_dev_sendto() for parameters and return values. Action
 * depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @coretags{mode-unrestricted, might-switch}
 */
ssize_t rtdm_sendto(int fd, const void *buf, size_t len, int flags,
		    const struct sockaddr *to, socklen_t tolen);

/**
 * @brief Transmit message to socket
 *
 * Refer to rt_dev_send() for parameters and return values. Action
 * depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @coretags{mode-unrestricted, might-switch}
 */
ssize_t rtdm_send(int fd, const void *buf, size_t len, int flags);

/**
 * @brief Bind to local address
 *
 * Refer to rt_dev_bind() for parameters and return values. Action
 * depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @coretags{task-unrestricted, might-switch}
 */
int rtdm_bind(int fd, const struct sockaddr *my_addr, socklen_t addrlen);

/**
 * @brief Connect to remote address
 *
 * Refer to rt_dev_connect() for parameters and return values. Action
 * depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @coretags{mode-unrestricted, might-switch}
 */
int rtdm_connect(int fd, const struct sockaddr *serv_addr, socklen_t addrlen);

/**
 * @brief Listen to incoming connection requests
 *
 * Refer to rt_dev_listen() for parameters and return values. Action
 * depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @coretags{task-unrestricted, might-switch}
 */
int rtdm_listen(int fd, int backlog);

/**
 * @brief Accept a connection request
 *
 * Refer to rt_dev_accept() for parameters and return values. Action
 * depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @coretags{mode-unrestricted, might-switch}
 */
int rtdm_accept(int fd, struct sockaddr *addr, socklen_t *addrlen);

/**
 * @brief Shut down parts of a connection
 *
 * Refer to rt_dev_shutdown() for parameters and return values. Action
 * depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @coretags{secondary-only, might-switch}
 */
int rtdm_shutdown(int fd, int how);

/**
 * @brief Get socket option
 *
 * Refer to rt_dev_getsockopt() for parameters and return values. Action
 * depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @coretags{task-unrestricted, might-switch}
 */
int rtdm_getsockopt(int fd, int level, int optname, void *optval,
		    socklen_t *optlen);

/**
 * @brief Set socket option
 *
 * Refer to rt_dev_setsockopt() for parameters and return values. Action
 * depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @coretags{task-unrestricted, might-switch}
 */
int rtdm_setsockopt(int fd, int level, int optname, const void *optval,
		    socklen_t optlen);

/**
 * @brief Get local socket address
 *
 * Refer to rt_dev_getsockname() for parameters and return values. Action
 * depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @coretags{task-unrestricted, might-switch}
 */
int rtdm_getsockname(int fd, struct sockaddr *name, socklen_t *namelen);

/**
 * @brief Get socket destination address
 *
 * Refer to rt_dev_getpeername() for parameters and return values. Action
 * depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @coretags{task-unrestricted, might-switch}
 */
int rtdm_getpeername(int fd, struct sockaddr *name, socklen_t *namelen);

/** @} Inter-driver calls */

/** @} */

/*!
 * @addtogroup rtdm_user_api
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
 * Action depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @see @c open() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 *
 * @coretags{secondary-only, might-switch}
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
 * Action depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @see @c socket() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 *
 * @coretags{secondary-only, might-switch}
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
 * Action depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @see @c close() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 *
 * @coretags{secondary-only, might-switch}
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
 * Action depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @see @c ioctl() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 *
 * @coretags{task-unrestricted, might-switch}
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
 * Action depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @see @c read() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 *
 * @coretags{mode-unrestricted, might-switch}
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
 * Action depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @see @c write() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 *
 * @coretags{mode-unrestricted, might-switch}
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
 * Action depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @see @c recvmsg() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 *
 * @coretags{mode-unrestricted, might-switch}
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
 * Action depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @see @c recvfrom() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 *
 * @coretags{mode-unrestricted, might-switch}
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
 * Action depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @see @c recv() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 *
 * @coretags{mode-unrestricted, might-switch}
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
 * Action depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @see @c sendmsg() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 *
 * @coretags{mode-unrestricted, might-switch}
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
 * Action depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @see @c sendto() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 *
 * @coretags{mode-unrestricted, might-switch}
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
 * Action depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @see @c send() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 *
 * @coretags{mode-unrestricted, might-switch}
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
 * Action depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @see @c bind() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 *
 * @coretags{mode-unrestricted, might-switch}
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
 * Action depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @see @c connect() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 *
 * @coretags{mode-unrestricted, might-switch}
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
 * Action depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @see @c listen() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 *
 * @coretags{task-unrestricted, might-switch}
 */
int rt_dev_listen(int fd, int backlog);

/**
 * @brief Accept connection requests
 *
 * @param[in] fd File descriptor as returned by rt_dev_socket()
 * @param[out] addr Buffer for remote address
 * @param[in,out] addrlen Address buffer size
 *
 * @return 0 on success, otherwise negative error code
 *
 * Action depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @see @c accept() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 *
 * @coretags{mode-unrestricted, might-switch}
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
 * Action depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @see @c shutdown() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 *
 * @coretags{secondary-only, might-switch}
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
 * Action depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @see @c getsockopt() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 *
 * @coretags{task-unrestricted, might-switch}
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
 * Action depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @see @c setsockopt() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 *
 * @coretags{task-unrestricted, might-switch}
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
 * Action depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @see @c getsockname() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 *
 * @coretags{task-unrestricted, might-switch}
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
 * Action depends on driver implementation, see @ref rtdm_profiles
 * "Device Profiles".
 *
 * @see @c getpeername() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 *
 * @coretags{task-unrestricted, might-switch}
 */
int rt_dev_getpeername(int fd, struct sockaddr *name, socklen_t *namelen);

#endif /* DOXYGEN_CPP */

/** @} */

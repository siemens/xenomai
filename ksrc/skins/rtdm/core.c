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

#include <linux/delay.h>

#include <nucleus/pod.h>
#include <nucleus/heap.h>
#include <rtdm/syscall.h>
#include <rtdm/rtdm_driver.h>

#include "rtdm/core.h"
#include "rtdm/device.h"


#define CLOSURE_RETRY_PERIOD    100 /* ms */

#define FD_BITMAP_SIZE  ((RTDM_FD_MAX + BITS_PER_LONG-1) / BITS_PER_LONG)

struct rtdm_fildes      fildes_table[RTDM_FD_MAX] =
                            { [0 ... RTDM_FD_MAX-1] = { NULL } };
static unsigned long    used_fildes[FD_BITMAP_SIZE];
int                     open_fildes;    /* amount of used descriptors */
xntbase_t               *rtdm_tbase;

#ifdef CONFIG_SMP
xnlock_t                rt_fildes_lock = XNARCH_LOCK_UNLOCKED;
#endif /* !CONFIG_SMP */


/**
 * @brief Resolve file descriptor to device context
 *
 * @param[in] fd File descriptor
 *
 * @return Pointer to associated device context, or NULL on error
 *
 * @note The device context has to be unlocked using rtdm_context_unlock()
 * when it is no longer referenced.
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
struct rtdm_dev_context *rtdm_context_get(int fd)
{
    struct rtdm_dev_context *context;
    spl_t                   s;


    if ((unsigned int)fd >= RTDM_FD_MAX)
        return NULL;

    xnlock_get_irqsave(&rt_fildes_lock, s);

    context = fildes_table[fd].context;
    if (unlikely(!context ||
                 test_bit(RTDM_CLOSING, &context->context_flags))) {
        xnlock_put_irqrestore(&rt_fildes_lock, s);
        return NULL;
    }

    rtdm_context_lock(context);

    xnlock_put_irqrestore(&rt_fildes_lock, s);

    return context;
}


static int create_instance(struct rtdm_device *device,
                           struct rtdm_dev_context **context_ptr,
                           struct rtdm_fildes **fildes_ptr,
                           rtdm_user_info_t *user_info,
                           int nrt_mem)
{
    struct rtdm_dev_context *context;
    int                     fd;
    spl_t                   s;


    /* Reset to NULL so that we can always use cleanup_instance to revert
       also partially successful allocations */
    *context_ptr = NULL;
    *fildes_ptr  = NULL;

    /* Reserve a file descriptor */
    xnlock_get_irqsave(&rt_fildes_lock, s);

    if (unlikely(open_fildes >= RTDM_FD_MAX)) {
        xnlock_put_irqrestore(&rt_fildes_lock, s);
        return -ENFILE;
    }

    fd = find_first_zero_bit(used_fildes, RTDM_FD_MAX);
    set_bit(fd, used_fildes);
    open_fildes++;

    xnlock_put_irqrestore(&rt_fildes_lock, s);

    *fildes_ptr = &fildes_table[fd];

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
        if (nrt_mem)
            context = kmalloc(sizeof(struct rtdm_dev_context) +
                              device->context_size, GFP_KERNEL);
        else
            context = xnmalloc(sizeof(struct rtdm_dev_context) +
                               device->context_size);
        if (unlikely(!context))
            return -ENOMEM;

        context->device = device;
    }

    *context_ptr = context;

    context->fd  = fd;
    context->ops = &device->ops;
    atomic_set(&context->close_lock_count, 0);
#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
    /* We use current->mm as cookie to identify the context owner */
    context->reserved.owner = user_info ? user_info->mm : NULL;
#else /* !__KERNEL__ || !CONFIG_XENO_OPT_PERVASIVE */
    context->reserved.owner = NULL;
#endif /* !__KERNEL__ || !CONFIG_XENO_OPT_PERVASIVE */

    return 0;
}


/* call with rt_fildes_lock acquired - will release it */
static void cleanup_instance(struct rtdm_device *device,
                             struct rtdm_dev_context *context,
                             struct rtdm_fildes *fildes,
                             int nrt_mem,
                             spl_t s)
{
    if (fildes) {
        clear_bit((fildes - fildes_table), used_fildes);
        fildes->context = NULL;
        open_fildes--;
    }

    xnlock_put_irqrestore(&rt_fildes_lock, s);

    if (context) {
        if (device->reserved.exclusive_context) {
            context->device = NULL;
        } else {
            if (nrt_mem)
                kfree(context);
            else
                xnfree(context);
        }
    }

    rtdm_dereference_device(device);
}


int _rtdm_open(rtdm_user_info_t *user_info, const char *path, int oflag)
{
    struct rtdm_device      *device;
    struct rtdm_fildes      *fildes;
    struct rtdm_dev_context *context;
    spl_t                   s;
    int                     ret;
    int                     nrt_mode = !rtdm_in_rt_context();


    device = get_named_device(path);
    ret = -ENODEV;
    if (!device)
        goto err_out;

    ret = create_instance(device, &context, &fildes, user_info, nrt_mode);
    if (ret != 0)
        goto cleanup_out;

    if (nrt_mode) {
        context->context_flags = (1 << RTDM_CREATED_IN_NRT);
        ret = device->open_nrt(context, user_info, oflag);
    } else {
        context->context_flags = 0;
        ret = device->open_rt(context, user_info, oflag);
    }

    XENO_ASSERT(RTDM, !rthal_local_irq_test(), rthal_local_irq_enable(););

    if (unlikely(ret < 0))
        goto cleanup_out;

    fildes->context = context;

    return context->fd;


 cleanup_out:
    xnlock_get_irqsave(&rt_fildes_lock, s);
    cleanup_instance(device, context, fildes, nrt_mode, s);

 err_out:
    return ret;
}


int _rtdm_socket(rtdm_user_info_t *user_info, int protocol_family,
                 int socket_type, int protocol)
{
    struct rtdm_device      *device;
    struct rtdm_fildes      *fildes;
    struct rtdm_dev_context *context;
    spl_t                   s;
    int                     ret;
    int                     nrt_mode = !rtdm_in_rt_context();


    device = get_protocol_device(protocol_family, socket_type);
    ret = -EAFNOSUPPORT;
    if (!device)
        goto err_out;

    ret = create_instance(device, &context, &fildes, user_info, nrt_mode);
    if (ret != 0)
        goto cleanup_out;

    if (nrt_mode) {
        context->context_flags = (1 << RTDM_CREATED_IN_NRT);
        ret = device->socket_nrt(context, user_info, protocol);
    } else {
        context->context_flags = 0;
        ret = device->socket_rt(context, user_info, protocol);
    }

    XENO_ASSERT(RTDM, !rthal_local_irq_test(), rthal_local_irq_enable(););

    if (unlikely(ret < 0))
        goto cleanup_out;

    fildes->context = context;

    return context->fd;


 cleanup_out:
    xnlock_get_irqsave(&rt_fildes_lock, s);
    cleanup_instance(device, context, fildes, nrt_mode, s);

 err_out:
    return ret;
}


int _rtdm_close(rtdm_user_info_t *user_info, int fd)
{
    struct rtdm_dev_context *context;
    spl_t                   s;
    int                     ret;


    ret = -EBADF;
    if (unlikely((unsigned int)fd >= RTDM_FD_MAX))
        goto err_out;

 again:
    xnlock_get_irqsave(&rt_fildes_lock, s);

    context = fildes_table[fd].context;

    if (unlikely(!context)) {
        xnlock_put_irqrestore(&rt_fildes_lock, s);
        goto err_out;   /* -EBADF */
    }

    set_bit(RTDM_CLOSING, &context->context_flags);
    rtdm_context_lock(context);

    xnlock_put_irqrestore(&rt_fildes_lock, s);

    if (rtdm_in_rt_context()) {
        ret = -ENOTSUPP;
        /* Warn about asymmetric open/close, but only if there is really a
           close_rt handler. Otherwise, we will be switched to nrt
           automatically. */
        if (unlikely(test_bit(RTDM_CREATED_IN_NRT, &context->context_flags) &&
                     (context->ops->close_rt !=
                         (rtdm_close_handler_t)rtdm_no_support))) {
            xnprintf("RTDM: closing device in real-time mode while creation "
                     "ran in non-real-time - this is not supported!\n");
            goto unlock_out;
        }

        ret = context->ops->close_rt(context, user_info);

    } else
        ret = context->ops->close_nrt(context, user_info);

    XENO_ASSERT(RTDM, !rthal_local_irq_test(), rthal_local_irq_enable(););

    if (unlikely(ret == -EAGAIN) && !rtdm_in_rt_context()) {
        msleep(CLOSURE_RETRY_PERIOD);
        goto again;
    } else if (unlikely(ret < 0))
        goto unlock_out;

    xnlock_get_irqsave(&rt_fildes_lock, s);

    if (unlikely(atomic_read(&context->close_lock_count) > 1)) {
        xnlock_put_irqrestore(&rt_fildes_lock, s);

        if (rtdm_in_rt_context()) {
            ret = -EAGAIN;
            goto unlock_out;
        }
        msleep(CLOSURE_RETRY_PERIOD);
        goto again;
    }

    cleanup_instance(context->device, context, &fildes_table[fd],
                     test_bit(RTDM_CREATED_IN_NRT, &context->context_flags),
                     s);

    return ret;


  unlock_out:
    rtdm_context_unlock(context);

  err_out:
    return ret;
}


void cleanup_owned_contexts(void *owner)
{
    struct rtdm_dev_context *context;
    unsigned int            fd;
    int                     ret;
    spl_t                   s;


    for (fd = 0; fd < RTDM_FD_MAX; fd++) {
        xnlock_get_irqsave(&rt_fildes_lock, s);

        context = fildes_table[fd].context;
        if (context && context->reserved.owner != owner)
            context = NULL;

        xnlock_put_irqrestore(&rt_fildes_lock, s);

        if (context) {
            ret = _rtdm_close(NULL, fd);
            XENO_ASSERT(RTDM, ret >= 0 || ret == -EBADF, ;);
        }
    }
}


#define MAJOR_FUNCTION_WRAPPER_TH(operation, args...)                       \
do {                                                                        \
    struct rtdm_dev_context *context;                                       \
    struct rtdm_operations  *ops;                                           \
    int                     ret;                                            \
                                                                            \
                                                                            \
    context = rtdm_context_get(fd);                                         \
    ret = -EBADF;                                                           \
    if (unlikely(!context))                                                 \
        goto err_out;                                                       \
                                                                            \
    ops = context->ops;                                                     \
                                                                            \
    if (rtdm_in_rt_context())                                               \
        ret = ops->operation##_rt(context, user_info, args);                \
    else                                                                    \
        ret = ops->operation##_nrt(context, user_info, args);               \
                                                                            \
    XENO_ASSERT(RTDM, !rthal_local_irq_test(), rthal_local_irq_enable();)

#define MAJOR_FUNCTION_WRAPPER_BH()                                         \
    rtdm_context_unlock(context);                                           \
                                                                            \
 err_out:                                                                   \
    return ret;                                                             \
} while (0)

#define MAJOR_FUNCTION_WRAPPER(operation, args...)                          \
do {                                                                        \
    MAJOR_FUNCTION_WRAPPER_TH(operation, args);                             \
    MAJOR_FUNCTION_WRAPPER_BH();                                            \
} while (0)


int _rtdm_ioctl(rtdm_user_info_t *user_info, int fd, int request, ...)
{
    va_list args;
    void    *arg;


    va_start(args, request);
    arg = va_arg(args, void *);
    va_end(args);

    MAJOR_FUNCTION_WRAPPER_TH(ioctl, (unsigned int)request, arg);

    if (unlikely(ret < 0) && (unsigned int)request == RTIOC_DEVICE_INFO) {
        struct rtdm_device *dev = context->device;
        struct rtdm_device_info dev_info;

        dev_info.device_flags = dev->device_flags;
        dev_info.device_class = dev->device_class;
        dev_info.device_sub_class = dev->device_sub_class;
        dev_info.profile_version = dev->profile_version;

        ret = rtdm_safe_copy_to_user(user_info, arg, &dev_info,
                                     sizeof(dev_info));
    }

    MAJOR_FUNCTION_WRAPPER_BH();
}


ssize_t _rtdm_read(rtdm_user_info_t *user_info, int fd, void *buf, size_t nbyte)
{
    MAJOR_FUNCTION_WRAPPER(read, buf, nbyte);
}


ssize_t _rtdm_write(rtdm_user_info_t *user_info, int fd, const void *buf,
                size_t nbyte)
{
    MAJOR_FUNCTION_WRAPPER(write, buf, nbyte);
}


ssize_t _rtdm_recvmsg(rtdm_user_info_t *user_info, int fd, struct msghdr *msg,
                  int flags)
{
    MAJOR_FUNCTION_WRAPPER(recvmsg, msg, flags);
}


ssize_t _rtdm_sendmsg(rtdm_user_info_t *user_info, int fd,
                  const struct msghdr *msg, int flags)
{
    MAJOR_FUNCTION_WRAPPER(sendmsg, msg, flags);
}


#ifdef DOXYGEN_CPP /* Only used for doxygen doc generation */

/**
 * @brief Increment context reference counter
 *
 * @param[in] context Device context
 *
 * @note rtdm_context_get() automatically increments the lock counter. You
 * only need to call this function in special scenrios.
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
 * @note Every successful call to rtdm_context_get() must be matched by a
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
 * @note Killing a real-time task that is blocked on some device operation can
 * lead to stalled file descriptors. To avoid such scenarios, always close the
 * device before explicitely terminating any real-time task which may use it.
 * To cleanup a stalled file descriptor, send its number to the @c open_fildes
 * /proc entry, e.g. via
 * @code #> echo 3 > /proc/xenomai/rtdm/open_fildes @endcode
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
                        struct sockaddr *from,
                        socklen_t *fromlen);

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


EXPORT_SYMBOL(rtdm_context_get);
EXPORT_SYMBOL(_rtdm_open);
EXPORT_SYMBOL(_rtdm_socket);
EXPORT_SYMBOL(_rtdm_close);
EXPORT_SYMBOL(_rtdm_ioctl);
EXPORT_SYMBOL(_rtdm_read);
EXPORT_SYMBOL(_rtdm_write);
EXPORT_SYMBOL(_rtdm_recvmsg);
EXPORT_SYMBOL(_rtdm_sendmsg);
EXPORT_SYMBOL(rtdm_tbase);

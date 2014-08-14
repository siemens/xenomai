/*
 * Copyright (C) 2005-2007 Jan Kiszka <jan.kiszka@web.de>
 * Copyright (C) 2005 Joerg Langenberg <joerg.langenberg@gmx.net>
 * Copyright (C) 2008,2013,2014 Gilles Chanteperdrix <gch@xenomai.org>.
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
 */
#ifndef _COBALT_KERNEL_FD_H
#define _COBALT_KERNEL_FD_H

#include <linux/types.h>
#include <linux/socket.h>
#include <cobalt/kernel/tree.h>

struct vm_area_struct;
struct rtdm_fd;
struct _rtdm_mmap_request;
struct xnselector;
struct xnsys_ppd;

/**
 * @file
 * @anchor File operation handlers
 * @addtogroup rtdm_device_register
 * @{
 */

/**
 * Open handler for named devices
 *
 * @param[in] fd File descriptor associated with opened device instance
 * @param[in] oflags Open flags as passed by the user
 *
 * The file descriptor carries a device minor information which can be
 * retrieved by a call to rtdm_fd_minor(fd). The minor number can be
 * used for distinguishing several instances of the same rtdm_device
 * type. Prior to entering this handler, the device minor information
 * may have been extracted from the pathname passed to the @a open()
 * call, according to the following rules:
 *
 * - RTDM first attempts to match the pathname exactly as passed by
 * the application, against the registered rtdm_device descriptors. On
 * success, the special minor -1 is assigned to @a fd and this handler
 * is called.
 *
 * - if the original pathname does not match any device descriptor, it
 * is scanned for the \@\<minor> suffix. If present, a second lookup is
 * performed only looking for the radix portion of the pathname
 * (i.e. stripping the suffix), and the file descriptor is assigned
 * the minor value retrieved earlier on success, at which point this
 * handler is called. When present, \<minor> must be a positive or null
 * decimal value, otherwise the open() call fails.
 *
 * For instance:
 *
 * @code
 *    fd = open("/dev/foo@0", ...); // rtdm_fd_minor(fd) == 0
 *    fd = open("/dev/foo@7", ...); // rtdm_fd_minor(fd) == 7
 *    fd = open("/dev/foo", ...);   // rtdm_fd_minor(fd) == -1
 * @endcode
 *
 * @note the device minor scheme is not supported by Xenomai 2.x.
 *
 * @return 0 on success. On failure, a negative error code is returned.
 *
 * @see @c open() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 */
int rtdm_open_handler(struct rtdm_fd *fd, int oflags);

/**
 * Socket creation handler for protocol devices
 *
 * @param[in] fd File descriptor associated with opened device instance
 * @param[in] protocol Protocol number as passed by the user
 *
 * @return 0 on success. On failure, a negative error code is returned.
 *
 * @see @c socket() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 */
int rtdm_socket_handler(struct rtdm_fd *fd, int protocol);

/**
 * Close handler
 *
 * @param[in] fd File descriptor associated with opened
 * device instance.
 *
 * @see @c close() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 */
void rtdm_close_handler(struct rtdm_fd *fd);

/**
 * IOCTL handler
 *
 * @param[in] fd File descriptor
 * @param[in] request Request number as passed by the user
 * @param[in,out] arg Request argument as passed by the user
 *
 * @return A positive value or 0 on success. On failure return either
 * -ENOSYS, to request that the function be called again from the opposite
 * realtime/non-realtime context, or another negative error code.
 *
 * @see @c ioctl() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 */
int rtdm_ioctl_handler(struct rtdm_fd *fd, unsigned int request, void __user *arg);

/**
 * Read handler
 *
 * @param[in] fd File descriptor
 * @param[out] buf Input buffer as passed by the user
 * @param[in] size Number of bytes the user requests to read
 *
 * @return On success, the number of bytes read. On failure return either
 * -ENOSYS, to request that this handler be called again from the opposite
 * realtime/non-realtime context, or another negative error code.
 *
 * @see @c read() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 */
ssize_t rtdm_read_handler(struct rtdm_fd *fd, void __user *buf, size_t size);

/**
 * Write handler
 *
 * @param[in] fd File descriptor
 * @param[in] buf Output buffer as passed by the user
 * @param[in] size Number of bytes the user requests to write
 *
 * @return On success, the number of bytes written. On failure return
 * either -ENOSYS, to request that this handler be called again from the
 * opposite realtime/non-realtime context, or another negative error code.
 *
 * @see @c write() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 */
ssize_t rtdm_write_handler(struct rtdm_fd *fd, const void __user *buf, size_t size);

/**
 * Receive message handler
 *
 * @param[in] fd File descriptor
 * @param[in,out] msg Message descriptor as passed by the user, automatically
 * mirrored to safe kernel memory in case of user mode call
 * @param[in] flags Message flags as passed by the user
 *
 * @return On success, the number of bytes received. On failure return
 * either -ENOSYS, to request that this handler be called again from the
 * opposite realtime/non-realtime context, or another negative error code.
 *
 * @see @c recvmsg() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 */
ssize_t rtdm_recvmsg_handler(struct rtdm_fd *fd, struct msghdr *msg, int flags);

/**
 * Transmit message handler
 *
 * @param[in] fd File descriptor
 * @param[in] msg Message descriptor as passed by the user, automatically
 * mirrored to safe kernel memory in case of user mode call
 * @param[in] flags Message flags as passed by the user
 *
 * @return On success, the number of bytes transmitted. On failure return
 * either -ENOSYS, to request that this handler be called again from the
 * opposite realtime/non-realtime context, or another negative error code.
 *
 * @see @c sendmsg() in IEEE Std 1003.1,
 * http://www.opengroup.org/onlinepubs/009695399
 */
ssize_t rtdm_sendmsg_handler(struct rtdm_fd *fd, const struct msghdr *msg, int flags);

/**
 * Select handler
 *
 * @param[in] fd File descriptor
 * @param selector Pointer to the selector structure
 * @param type Type of events (@a XNSELECT_READ, @a XNSELECT_WRITE, or @a
 * XNSELECT_EXCEPT)
 * @param index Index of the file descriptor
 *
 * @return 0 on success. On failure, a negative error code is
 * returned.
 *
 * @see @c select() in POSIX.1-2001,
 * http://pubs.opengroup.org/onlinepubs/007908799/xsh/select.html
 */
int rtdm_select_handler(struct rtdm_fd *fd, struct xnselector *selector,
			unsigned int type, unsigned int index);

/**
 * Memory mapping handler
 *
 * @param[in] fd File descriptor
 * @param[in] vma Virtual memory area descriptor
 *
 * @return 0 on success. On failure, a negative error code is
 * returned.
 *
 * @see @c mmap() in POSIX.1-2001,
 * http://pubs.opengroup.org/onlinepubs/7908799/xsh/mmap.html
 */
int rtdm_mmap_handler(struct rtdm_fd *fd, struct vm_area_struct *vma);

/**
 * @anchor rtdm_fd_ops
 * @brief RTDM file operation descriptor.
 *
 * This structure describes the operations available with a RTDM
 * device, defining handlers for submitting I/O requests. Those
 * handlers are implemented by RTDM device drivers.
 */
struct rtdm_fd_ops {
	/** See rtdm_open_handler(). */
	int (*open)(struct rtdm_fd *fd, int oflags);
	/** See rtdm_socket_handler(). */
	int (*socket)(struct rtdm_fd *fd, int protocol);
	/** See rtdm_close_handler(). */
	void (*close)(struct rtdm_fd *fd);
	/** See rtdm_ioctl_handler(). */
	int (*ioctl_rt)(struct rtdm_fd *fd,
			unsigned int request, void __user *arg);
	/** See rtdm_ioctl_handler(). */
	int (*ioctl_nrt)(struct rtdm_fd *fd,
			 unsigned int request, void __user *arg);
	/** See rtdm_read_handler(). */
	ssize_t (*read_rt)(struct rtdm_fd *fd,
			   void __user *buf, size_t size);
	/** See rtdm_read_handler(). */
	ssize_t (*read_nrt)(struct rtdm_fd *fd,
			    void __user *buf, size_t size);
	/** See rtdm_write_handler(). */
	ssize_t (*write_rt)(struct rtdm_fd *fd,
			    const void __user *buf, size_t size);
	/** See rtdm_write_handler(). */
	ssize_t (*write_nrt)(struct rtdm_fd *fd,
			     const void __user *buf, size_t size);
	/** See rtdm_recvmsg_handler(). */
	ssize_t (*recvmsg_rt)(struct rtdm_fd *fd,
			      struct msghdr *msg, int flags);
	/** See rtdm_recvmsg_handler(). */
	ssize_t (*recvmsg_nrt)(struct rtdm_fd *fd,
			       struct msghdr *msg, int flags);
	/** See rtdm_sendmsg_handler(). */
	ssize_t (*sendmsg_rt)(struct rtdm_fd *fd,
			      const struct msghdr *msg, int flags);
	/** See rtdm_sendmsg_handler(). */
	ssize_t (*sendmsg_nrt)(struct rtdm_fd *fd,
			       const struct msghdr *msg, int flags);
	/** See rtdm_select_handler(). */
	int (*select)(struct rtdm_fd *fd,
		      struct xnselector *selector,
		      unsigned int type, unsigned int index);
	/** See rtdm_mmap_handler(). */
	int (*mmap)(struct rtdm_fd *fd,
		    struct vm_area_struct *vma);
};

/** @} File operation handlers */

struct rtdm_fd {
	unsigned int magic;
	struct rtdm_fd_ops *ops;
	struct xnsys_ppd *cont;
	unsigned int refs;
	int minor;
	struct list_head cleanup;
};

struct rtdm_fd_index {
	struct xnid id;
	struct rtdm_fd *fd;
};

#define XNFD_MAGIC_ANY 0

static inline struct xnsys_ppd *rtdm_fd_owner(struct rtdm_fd *fd)
{
	return fd->cont;
}

static inline int rtdm_fd_minor(struct rtdm_fd *fd)
{
	return fd->minor;
}

int rtdm_fd_enter(struct xnsys_ppd *p, struct rtdm_fd *rtdm_fd, int ufd,
		  unsigned int magic, struct rtdm_fd_ops *ops);

struct rtdm_fd *rtdm_fd_get(struct xnsys_ppd *p,
			    int ufd, unsigned int magic);

int rtdm_fd_lock(struct rtdm_fd *fd);

void rtdm_fd_put(struct rtdm_fd *fd);

void rtdm_fd_unlock(struct rtdm_fd *fd);

int rtdm_fd_ioctl(struct xnsys_ppd *p, int ufd, unsigned int request, ...);

ssize_t rtdm_fd_read(struct xnsys_ppd *p, int ufd,
		     void __user *buf, size_t size);

ssize_t rtdm_fd_write(struct xnsys_ppd *p, int ufd,
		      const void __user *buf, size_t size);

int rtdm_fd_close(struct xnsys_ppd *p, int ufd, unsigned int magic);

ssize_t rtdm_fd_recvmsg(struct xnsys_ppd *p, int ufd,
			struct msghdr *msg, int flags);

ssize_t rtdm_fd_sendmsg(struct xnsys_ppd *p, int ufd,
			const struct msghdr *msg, int flags);

int rtdm_fd_mmap(struct xnsys_ppd *p, int ufd,
		 struct _rtdm_mmap_request *rma,
		 void * __user *u_addrp);

int rtdm_fd_valid_p(int ufd);

int rtdm_fd_select(int ufd, struct xnselector *selector,
		   unsigned int type);

void rtdm_fd_cleanup(struct xnsys_ppd *p);

void rtdm_fd_init(void);

#endif /* _COBALT_KERNEL_FD_H */

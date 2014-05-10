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

struct rtdm_fd;
struct xnselector;
struct xnsys_ppd;

/**
 * IOCTL handler
 *
 * @param[in] fd File descriptor structure
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
typedef int rtdm_fd_ioctl_t(struct rtdm_fd *fd, unsigned int request, void __user *arg);

/**
 * Read handler
 *
 * @param[in] fd File descriptor structure
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
typedef ssize_t rtdm_fd_read_t(struct rtdm_fd *fd, void __user *buf, size_t size);

/**
 * Write handler
 *
 * @param[in] fd File descriptor structure
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
typedef ssize_t rtdm_fd_write_t(struct rtdm_fd *fd, const void __user *buf, size_t size);

/**
 * Receive message handler
 *
 * @param[in] fd File descriptor structure
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
typedef ssize_t rtdm_fd_recvmsg_t(struct rtdm_fd *fd, struct msghdr *msg, int flags);

/**
 * Transmit message handler
 *
 * @param[in] fd File descriptor structure
 * @param[in] user_info Opaque pointer to information about user mode caller,
 * NULL if kernel mode call
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
typedef ssize_t rtdm_fd_sendmsg_t(struct rtdm_fd *fd, const struct msghdr *msg, int flags);

struct rtdm_fd_ops {
	rtdm_fd_ioctl_t *ioctl_rt;
	rtdm_fd_ioctl_t *ioctl_nrt;
	rtdm_fd_read_t *read_rt;
	rtdm_fd_read_t *read_nrt;
	rtdm_fd_write_t *write_rt;
	rtdm_fd_write_t *write_nrt;
	rtdm_fd_recvmsg_t *recvmsg_rt;
	rtdm_fd_recvmsg_t *recvmsg_nrt;
	rtdm_fd_sendmsg_t *sendmsg_rt;
	rtdm_fd_sendmsg_t *sendmsg_nrt;
	int (*select_bind)(struct rtdm_fd *fd, struct xnselector *selector,
			unsigned type, unsigned index);
	void (*close)(struct rtdm_fd *fd);
};

struct rtdm_fd {
	unsigned magic;
	struct rtdm_fd_ops *ops;
	struct xnsys_ppd *cont;
	unsigned refs;
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

int rtdm_fd_enter(struct xnsys_ppd *p, struct rtdm_fd *rtdm_fd, int ufd,
	unsigned magic, struct rtdm_fd_ops *ops);

struct rtdm_fd *rtdm_fd_get(struct xnsys_ppd *p, int ufd, unsigned magic);

int rtdm_fd_lock(struct rtdm_fd *fd);

void rtdm_fd_put(struct rtdm_fd *fd);

void rtdm_fd_unlock(struct rtdm_fd *fd);

int rtdm_fd_ioctl(struct xnsys_ppd *p, int fd, unsigned request, ...);

ssize_t rtdm_fd_read(struct xnsys_ppd *p, int fd, void __user *buf, size_t size);

ssize_t
rtdm_fd_write(struct xnsys_ppd *p, int fd, const void __user *buf, size_t size);

int rtdm_fd_close(struct xnsys_ppd *p, int fd, unsigned magic);

ssize_t
rtdm_fd_recvmsg(struct xnsys_ppd *p, int fd, struct msghdr *msg, int flags);

ssize_t
rtdm_fd_sendmsg(struct xnsys_ppd *p, int fd, const struct msghdr *msg, int flags);

int rtdm_fd_valid_p(int ufd);

int rtdm_fd_select_bind(int ufd, struct xnselector *selector, unsigned type);

void rtdm_fd_cleanup(struct xnsys_ppd *p);

void rtdm_fd_init(void);

#endif /* _COBALT_KERNEL_FD_H */

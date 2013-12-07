/**
 * @file
 * @note Copyright (C) 2005, 2006 Jan Kiszka <jan.kiszka@web.de>
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
#ifndef _COBALT_RTDM_RTDM_H
#define _COBALT_RTDM_RTDM_H

#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/ioctl.h>
#include <linux/sched.h>
#include <linux/socket.h>

typedef u32 socklen_t;
typedef struct task_struct rtdm_user_info_t;

int __rt_dev_open(rtdm_user_info_t *user_info,
		  const char *path, int oflag);

int __rt_dev_socket(rtdm_user_info_t *user_info,
		    int protocol_family,
		    int socket_type, int protocol);

int __rt_dev_close(rtdm_user_info_t *user_info,
		   int fd);

int __rt_dev_ioctl(rtdm_user_info_t *user_info,
		   int fd, int request, ...);

ssize_t __rt_dev_read(rtdm_user_info_t *user_info,
		      int fd, void *buf,
		      size_t nbyte);

ssize_t __rt_dev_write(rtdm_user_info_t *user_info,
		       int fd, const void *buf,
		       size_t nbyte);

ssize_t __rt_dev_recvmsg(rtdm_user_info_t *user_info,
			 int fd, struct msghdr *msg, int flags);

ssize_t __rt_dev_sendmsg(rtdm_user_info_t *user_info,
			 int fd, const struct msghdr *msg, int flags);

/*
 * Define RTDM_NO_DEFAULT_USER_API to switch off the default
 * rt_dev_xxx interface when providing a customised user API.
 */
#ifndef RTDM_NO_DEFAULT_USER_API

#define rt_dev_open(path, oflag, ...)				\
	__rt_dev_open(NULL, path, oflag)

#define rt_dev_socket(protocol_family, socket_type, protocol)	\
	__rt_dev_socket(NULL, protocol_family, socket_type, protocol)

#define rt_dev_close(fd)					\
	__rt_dev_close(NULL, fd)

#define rt_dev_ioctl(fd, request, ...)				\
	__rt_dev_ioctl(NULL, fd, request, __VA_ARGS__)

#define rt_dev_read(fd, buf, nbyte)				\
	__rt_dev_read(NULL, fd, buf, nbyte)

#define rt_dev_write(fd, buf, nbyte)				\
	__rt_dev_write(NULL, fd, buf, nbyte)

#define rt_dev_recvmsg(fd, msg, flags)				\
	__rt_dev_recvmsg(NULL, fd, msg, flags)

#define rt_dev_sendmsg(fd, msg, flags)				\
	__rt_dev_sendmsg(NULL, fd, msg, flags)

static inline
ssize_t rt_dev_recvfrom(int fd, void *buf, size_t len, int flags,
			struct sockaddr *from,
			socklen_t *fromlen)
{
	struct iovec iov;
	struct msghdr msg;
	int ret;

	iov.iov_base = buf;
	iov.iov_len = len;

	msg.msg_name = from;
	msg.msg_namelen = from ? *fromlen : 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;

	ret = rt_dev_recvmsg(fd, &msg, flags);
	if (ret >= 0 && from)
		*fromlen = msg.msg_namelen;
	return ret;
}

#endif /* !RTDM_NO_DEFAULT_USER_API */

#include <rtdm/uapi/rtdm.h>

#endif /* _COBALT_RTDM_RTDM_H */

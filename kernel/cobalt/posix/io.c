/*
 * Copyright (C) 2005 Jan Kiszka <jan.kiszka@web.de>.
 * Copyright (C) 2005 Joerg Langenberg <joerg.langenberg@gmx.net>.
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
#include <linux/err.h>
#include <cobalt/kernel/ppd.h>
#include <xenomai/rtdm/internal.h>
#include "io.h"

int cobalt_open(int fd, const char __user *u_path, int oflag)
{
	char krnl_path[RTDM_MAX_DEVNAME_LEN + 1];

	if (unlikely(__xn_safe_strncpy_from_user(krnl_path, u_path,
						 sizeof(krnl_path) - 1) < 0))
		return -EFAULT;

	krnl_path[sizeof(krnl_path) - 1] = '\0';

	return __rt_dev_open(xnsys_ppd_get(0), fd, krnl_path, oflag);
}

int cobalt_socket(int fd, int protocol_family,
		  int socket_type, int protocol)
{
	return __rt_dev_socket(xnsys_ppd_get(0), fd,
			protocol_family, socket_type, protocol);
}

int cobalt_ioctl(int fd, unsigned int request, void __user *arg)
{
	return rtdm_fd_ioctl(xnsys_ppd_get(0), fd, request, arg);
}

ssize_t cobalt_read(int fd, void __user *buf, size_t size)
{
	return rtdm_fd_read(xnsys_ppd_get(0), fd, buf, size);
}

ssize_t cobalt_write(int fd, const void __user *buf, size_t size)
{
	return rtdm_fd_write(xnsys_ppd_get(0), fd, buf, size);
}

ssize_t cobalt_recvmsg(int fd, struct msghdr __user *umsg, int flags)
{
	struct msghdr m;
	int ret;

	if (__xn_copy_from_user(&m, umsg, sizeof(m)))
		return -EFAULT;

	ret = rtdm_fd_recvmsg(xnsys_ppd_get(0), fd, &m, flags);
	if (ret < 0)
		return ret;

	if (__xn_copy_to_user(umsg, &m, sizeof(*umsg)))
		return -EFAULT;

	return ret;
}

ssize_t cobalt_sendmsg(int fd, struct msghdr __user *umsg, int flags)
{
	struct msghdr m;

	if (__xn_copy_from_user(&m, umsg, sizeof(m)))
		return -EFAULT;

	return rtdm_fd_sendmsg(xnsys_ppd_get(0), fd, &m, flags);
}

int cobalt_close(int fd)
{
	return rtdm_fd_close(xnsys_ppd_get(0), fd, XNFD_MAGIC_ANY);
}

int cobalt_mmap(int fd, struct _rtdm_mmap_request __user *u_rma,
                  void __user **u_addrp)
{
	struct _rtdm_mmap_request rma;
	void *u_addr;
	int ret;

	if (__xn_copy_from_user(&rma, u_rma, sizeof(rma)))
		return -EFAULT;

	ret = rtdm_fd_mmap(xnsys_ppd_get(0), fd, &rma, &u_addr);
	if (ret)
		return ret;

	if (__xn_copy_to_user(u_addrp, &u_addr, sizeof(u_addr)))
		return -EFAULT;

	return 0;
}

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
#ifndef _COBALT_POSIX_IO_H
#define _COBALT_POSIX_IO_H

#include <rtdm/rtdm.h>

int cobalt_open(int fd, const char __user *u_path, int oflag);

int cobalt_socket(int fd, int protocol_family,
		  int socket_type, int protocol);

int cobalt_ioctl(int fd, unsigned int request, void __user *arg);

ssize_t cobalt_read(int fd, void __user *buf, size_t size);

ssize_t cobalt_write(int fd, const void __user *buf, size_t size);

ssize_t cobalt_recvmsg(int fd, struct msghdr __user *umsg, int flags);

ssize_t cobalt_sendmsg(int fd, struct msghdr __user *umsg, int flags);

int cobalt_close(int fd);

int cobalt_mmap(int fd, struct _rtdm_mmap_request __user *u_rma,
		void __user **u_addrp);

int cobalt_select(int nfds,
		  fd_set __user *u_rfds,
		  fd_set __user *u_wfds,
		  fd_set __user *u_xfds,
		  struct timeval __user *u_tv);

#endif /* !_COBALT_POSIX_IO_H */

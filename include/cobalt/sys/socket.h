/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _XENO_POSIX_SOCKET_H
#define _XENO_POSIX_SOCKET_H

#ifndef __KERNEL__

#include_next <sys/socket.h>
#include <cobalt/wrappers.h>

#ifdef __cplusplus
extern "C" {
#endif

COBALT_DECL(int, socket(int protocol_family,
			int socket_type, int protocol));

COBALT_DECL(ssize_t, recvmsg(int fd,
			     struct msghdr *msg, int flags));

COBALT_DECL(ssize_t, sendmsg(int fd,
			     const struct msghdr *msg, int flags));

COBALT_DECL(ssize_t, recvfrom(int fd, void *buf, size_t len, int flags,
			      struct sockaddr *from, socklen_t *fromlen));

COBALT_DECL(ssize_t, sendto(int fd, const void *buf, size_t len, int flags,
			    const struct sockaddr *to, socklen_t tolen));

COBALT_DECL(ssize_t, recv(int fd, void *buf,
			  size_t len, int flags));

COBALT_DECL(ssize_t, send(int fd, const void *buf,
			  size_t len, int flags));

COBALT_DECL(int, getsockopt(int fd, int level, int optname,
			    void *optval, socklen_t *optlen));

COBALT_DECL(int, setsockopt(int fd, int level, int optname,
			    const void *optval, socklen_t optlen));

COBALT_DECL(int, bind(int fd, const struct sockaddr *my_addr,
		      socklen_t addrlen));

COBALT_DECL(int, connect(int fd, const struct sockaddr *serv_addr,
			 socklen_t addrlen));

COBALT_DECL(int, listen(int fd, int backlog));

COBALT_DECL(int, accept(int fd, struct sockaddr *addr,
			socklen_t *addrlen));

COBALT_DECL(int, getsockname(int fd, struct sockaddr *name,
			     socklen_t *namelen));

COBALT_DECL(int, getpeername(int fd, struct sockaddr *name,
			     socklen_t *namelen));

COBALT_DECL(int, shutdown(int fd, int how));

#ifdef __cplusplus
}
#endif

#endif /* !__KERNEL__ */

#endif /* _XENO_POSIX_SOCKET_H */

/*
 * Copyright (C) 2005 Jan Kiszka <jan.kiszka@web.de>.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
  USA.
 */

#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <rtdm/rtdm.h>
#include <cobalt/uapi/rtdm/syscall.h>
#include <asm/xenomai/syscall.h>

extern int __rtdm_muxid;
extern int __rtdm_fd_start;

static inline int set_errno(int ret)
{
	if (ret >= 0)
		return ret;

	errno = -ret;
	return -1;
}

COBALT_IMPL(int, open, (const char *path, int oflag, ...))
{
	int ret, oldtype;
	const char *rtdm_path = path;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	/* skip path prefix for RTDM invocation */
	if (strncmp(path, "/dev/", 5) == 0)
		rtdm_path += 5;

	ret = XENOMAI_SKINCALL2(__rtdm_muxid, sc_rtdm_open, rtdm_path, oflag);

	pthread_setcanceltype(oldtype, NULL);

	if (ret >= 0)
		ret += __rtdm_fd_start;
	else if (ret == -ENODEV || ret == -ENOSYS) {
		va_list ap;

		va_start(ap, oflag);

		ret = __STD(open(path, oflag, va_arg(ap, mode_t)));

		va_end(ap);

		if (ret >= __rtdm_fd_start) {
			__STD(close(ret));
			errno = EMFILE;
			ret = -1;
		}
	} else {
		errno = -ret;
		ret = -1;
	}

	return ret;
}

COBALT_IMPL(int, socket, (int protocol_family, int socket_type, int protocol))
{
	int ret;

	ret = XENOMAI_SKINCALL3(__rtdm_muxid,
				sc_rtdm_socket,
				protocol_family, socket_type, protocol);
	if (ret >= 0)
		ret += __rtdm_fd_start;
	else if (ret == -EAFNOSUPPORT || ret == -EPROTONOSUPPORT || 
		 ret == -ENOSYS) {
		ret = __STD(socket(protocol_family, socket_type, protocol));

		if (ret >= __rtdm_fd_start) {
			__STD(close(ret));
			errno = -EMFILE;
			ret = -1;
		}
	} else {
		errno = -ret;
		ret = -1;
	}

	return ret;
}

COBALT_IMPL(int, close, (int fd))
{
	int ret;

	if (fd >= __rtdm_fd_start) {
		int oldtype;

		pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

		ret = set_errno(XENOMAI_SKINCALL1(__rtdm_muxid,
						  sc_rtdm_close,
						  fd - __rtdm_fd_start));

		pthread_setcanceltype(oldtype, NULL);

		return ret;
	} else
		return __STD(close(fd));

	return ret;
}

COBALT_IMPL(int, ioctl, (int fd, unsigned long int request, ...))
{
	va_list ap;
	void *arg;

	va_start(ap, request);
	arg = va_arg(ap, void *);
	va_end(ap);

	if (fd >= __rtdm_fd_start)
		return set_errno(XENOMAI_SKINCALL3(__rtdm_muxid,
						   sc_rtdm_ioctl,
						   fd - __rtdm_fd_start,
						   request, arg));
	else
		return __STD(ioctl(fd, request, arg));
}

COBALT_IMPL(ssize_t, read, (int fd, void *buf, size_t nbyte))
{
	if (fd >= __rtdm_fd_start) {
		int ret, oldtype;

		pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

		ret = set_errno(XENOMAI_SKINCALL3(__rtdm_muxid,
						  sc_rtdm_read,
						  fd - __rtdm_fd_start,
						  buf, nbyte));

		pthread_setcanceltype(oldtype, NULL);

		return ret;
	} else
		return __STD(read(fd, buf, nbyte));
}

COBALT_IMPL(ssize_t, write, (int fd, const void *buf, size_t nbyte))
{
	if (fd >= __rtdm_fd_start) {
		int ret, oldtype;

		pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

		ret = set_errno(XENOMAI_SKINCALL3(__rtdm_muxid,
						  sc_rtdm_write,
						  fd - __rtdm_fd_start,
						  buf, nbyte));

		pthread_setcanceltype(oldtype, NULL);

		return ret;
	} else
		return __STD(write(fd, buf, nbyte));
}

COBALT_IMPL(ssize_t, recvmsg, (int fd, struct msghdr * msg, int flags))
{
	if (fd >= __rtdm_fd_start) {
		int ret, oldtype;

		pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

		ret = set_errno(XENOMAI_SKINCALL3(__rtdm_muxid,
						  sc_rtdm_recvmsg,
						  fd - __rtdm_fd_start,
						  msg, flags));

		pthread_setcanceltype(oldtype, NULL);

		return ret;
	} else
		return __STD(recvmsg(fd, msg, flags));
}

COBALT_IMPL(ssize_t, sendmsg, (int fd, const struct msghdr * msg, int flags))
{
	if (fd >= __rtdm_fd_start) {
		int ret, oldtype;

		pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

		ret = set_errno(XENOMAI_SKINCALL3(__rtdm_muxid,
						  sc_rtdm_sendmsg,
						  fd - __rtdm_fd_start,
						  msg, flags));

		pthread_setcanceltype(oldtype, NULL);

		return ret;
	} else
		return __STD(sendmsg(fd, msg, flags));
}

COBALT_IMPL(ssize_t, recvfrom, (int fd, void *buf, size_t len, int flags,
				struct sockaddr * from, socklen_t * fromlen))
{
	if (fd >= __rtdm_fd_start) {
		struct iovec iov = { buf, len };
		struct msghdr msg =
		    { from, (from != NULL) ? *fromlen : 0, &iov, 1, NULL, 0 };
		int ret, oldtype;

		pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

		ret = XENOMAI_SKINCALL3(__rtdm_muxid,
					sc_rtdm_recvmsg,
					fd - __rtdm_fd_start, &msg, flags);

		pthread_setcanceltype(oldtype, NULL);

		if (ret < 0) {
			errno = -ret;
			ret = -1;
		} else if (from != NULL)
			*fromlen = msg.msg_namelen;
		return ret;
	} else
		return __STD(recvfrom(fd, buf, len, flags, from, fromlen));
}

COBALT_IMPL(ssize_t, sendto, (int fd, const void *buf, size_t len, int flags,
			      const struct sockaddr * to, socklen_t tolen))
{
	if (fd >= __rtdm_fd_start) {
		struct iovec iov = { (void *)buf, len };
		struct msghdr msg =
		    { (struct sockaddr *)to, tolen, &iov, 1, NULL, 0 };
		int ret, oldtype;

		pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

		ret = set_errno(XENOMAI_SKINCALL3(__rtdm_muxid,
						  sc_rtdm_sendmsg,
						  fd - __rtdm_fd_start,
						  &msg, flags));

		pthread_setcanceltype(oldtype, NULL);

		return ret;
	} else
		return __STD(sendto(fd, buf, len, flags, to, tolen));
}

COBALT_IMPL(ssize_t, recv, (int fd, void *buf, size_t len, int flags))
{
	if (fd >= __rtdm_fd_start) {
		struct iovec iov = { buf, len };
		struct msghdr msg = { NULL, 0, &iov, 1, NULL, 0 };
		int ret, oldtype;

		pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

		ret = set_errno(XENOMAI_SKINCALL3(__rtdm_muxid,
						  sc_rtdm_recvmsg,
						  fd - __rtdm_fd_start,
						  &msg, flags));

		pthread_setcanceltype(oldtype, NULL);

		return ret;
	} else
		return __STD(recv(fd, buf, len, flags));
}

COBALT_IMPL(ssize_t, send, (int fd, const void *buf, size_t len, int flags))
{
	if (fd >= __rtdm_fd_start) {
		struct iovec iov = { (void *)buf, len };
		struct msghdr msg = { NULL, 0, &iov, 1, NULL, 0 };
		int ret, oldtype;

		pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

		ret = set_errno(XENOMAI_SKINCALL3(__rtdm_muxid,
						  sc_rtdm_sendmsg,
						  fd - __rtdm_fd_start,
						  &msg, flags));

		pthread_setcanceltype(oldtype, NULL);

		return ret;
	} else
		return __STD(send(fd, buf, len, flags));
}

COBALT_IMPL(int, getsockopt, (int fd, int level, int optname, void *optval,
			      socklen_t * optlen))
{
	if (fd >= __rtdm_fd_start) {
		struct _rtdm_getsockopt_args args =
		    { level, optname, optval, optlen };

		return set_errno(XENOMAI_SKINCALL3(__rtdm_muxid,
						   sc_rtdm_ioctl,
						   fd - __rtdm_fd_start,
						   _RTIOC_GETSOCKOPT, &args));
	} else
		return __STD(getsockopt(fd, level, optname, optval, optlen));
}

COBALT_IMPL(int, setsockopt, (int fd, int level, int optname, const void *optval,
			      socklen_t optlen))
{
	if (fd >= __rtdm_fd_start) {
		struct _rtdm_setsockopt_args args =
		    { level, optname, (void *)optval, optlen };

		return set_errno(XENOMAI_SKINCALL3(__rtdm_muxid,
						   sc_rtdm_ioctl,
						   fd - __rtdm_fd_start,
						   _RTIOC_SETSOCKOPT, &args));
	} else
		return __STD(setsockopt(fd, level, optname, optval, optlen));
}

COBALT_IMPL(int, bind, (int fd, const struct sockaddr *my_addr, socklen_t addrlen))
{
	if (fd >= __rtdm_fd_start) {
		struct _rtdm_setsockaddr_args args = { my_addr, addrlen };

		return set_errno(XENOMAI_SKINCALL3(__rtdm_muxid,
						   sc_rtdm_ioctl,
						   fd - __rtdm_fd_start,
						   _RTIOC_BIND, &args));
	} else
		return __STD(bind(fd, my_addr, addrlen));
}

COBALT_IMPL(int, connect, (int fd, const struct sockaddr *serv_addr, socklen_t addrlen))
{
	if (fd >= __rtdm_fd_start) {
		struct _rtdm_setsockaddr_args args = { serv_addr, addrlen };
		int ret, oldtype;

		pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

		ret = set_errno(XENOMAI_SKINCALL3(__rtdm_muxid,
						  sc_rtdm_ioctl,
						  fd - __rtdm_fd_start,
						  _RTIOC_CONNECT, &args));

		pthread_setcanceltype(oldtype, NULL);

		return ret;
	} else
		return __STD(connect(fd, serv_addr, addrlen));
}

COBALT_IMPL(int, listen, (int fd, int backlog))
{
	if (fd >= __rtdm_fd_start) {
		return set_errno(XENOMAI_SKINCALL3(__rtdm_muxid,
						   sc_rtdm_ioctl,
						   fd - __rtdm_fd_start,
						   _RTIOC_LISTEN, backlog));
	} else
		return __STD(listen(fd, backlog));
}

COBALT_IMPL(int, accept, (int fd, struct sockaddr *addr, socklen_t * addrlen))
{
	if (fd >= __rtdm_fd_start) {
		struct _rtdm_getsockaddr_args args = { addr, addrlen };
		int oldtype;

		pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

		fd = XENOMAI_SKINCALL3(__rtdm_muxid,
				       sc_rtdm_ioctl,
				       fd - __rtdm_fd_start,
				       _RTIOC_ACCEPT, &args);

		pthread_setcanceltype(oldtype, NULL);

		if (fd < 0)
			return set_errno(fd);

		return fd + __rtdm_fd_start;
	} else {
		fd = __STD(accept(fd, addr, addrlen));

		if (fd >= __rtdm_fd_start) {
			__STD(close(fd));
			errno = EMFILE;
			fd = -1;
		}

		return fd;
	}
}

COBALT_IMPL(int, getsockname, (int fd, struct sockaddr *name, socklen_t *namelen))
{
	if (fd >= __rtdm_fd_start) {
		struct _rtdm_getsockaddr_args args = { name, namelen };

		return set_errno(XENOMAI_SKINCALL3(__rtdm_muxid,
						   sc_rtdm_ioctl,
						   fd - __rtdm_fd_start,
						   _RTIOC_GETSOCKNAME, &args));
	} else
		return __STD(getsockname(fd, name, namelen));
}

COBALT_IMPL(int, getpeername, (int fd, struct sockaddr *name, socklen_t *namelen))
{
	if (fd >= __rtdm_fd_start) {
		struct _rtdm_getsockaddr_args args = { name, namelen };

		return set_errno(XENOMAI_SKINCALL3(__rtdm_muxid,
						   sc_rtdm_ioctl,
						   fd - __rtdm_fd_start,
						   _RTIOC_GETPEERNAME, &args));
	} else
		return __STD(getpeername(fd, name, namelen));
}

COBALT_IMPL(int, shutdown, (int fd, int how))
{
	if (fd >= __rtdm_fd_start) {
		return set_errno(XENOMAI_SKINCALL3(__rtdm_muxid,
						   sc_rtdm_ioctl,
						   fd - __rtdm_fd_start,
						   _RTIOC_SHUTDOWN, how));
	} else
		return __STD(shutdown(fd, how));
}

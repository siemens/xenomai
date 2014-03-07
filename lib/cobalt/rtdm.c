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
#include <cobalt/uapi/syscall.h>
#include <asm/xenomai/syscall.h>

extern int __rtdm_muxid;
extern int __cobalt_muxid;

static inline int set_errno(int ret)
{
	if (ret >= 0)
		return ret;

	errno = -ret;
	return -1;
}

COBALT_IMPL(int, open, (const char *path, int oflag, ...))
{
	int ret, fd, oldtype;
	const char *rtdm_path = path;
	va_list ap;

	fd = __STD(open("/dev/null", O_RDONLY));
	if (fd < 0)
		return fd;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	/* skip path prefix for RTDM invocation */
	if (strncmp(path, "/dev/", 5) == 0)
		rtdm_path += 5;

	ret = XENOMAI_SKINCALL3(__rtdm_muxid,
				sc_rtdm_open,
				fd, rtdm_path, oflag);

	pthread_setcanceltype(oldtype, NULL);

	if (ret == fd)
		return fd;
	__STD(close(fd));

	if (ret != -ENODEV && ret != -ENOSYS)
		return set_errno(ret);

	va_start(ap, oflag);
	ret = __STD(open(path, oflag, va_arg(ap, mode_t)));
	va_end(ap);

	return ret;
}

COBALT_IMPL(int, socket, (int protocol_family, int socket_type, int protocol))
{
	int ret, fd;

	fd = __STD(open("/dev/null", O_RDONLY));
	if (fd < 0)
		return fd;

	ret = XENOMAI_SKINCALL4(__rtdm_muxid,
				sc_rtdm_socket,
				fd, protocol_family, socket_type, protocol);
	if (ret == fd)
		return fd;
	__STD(close(fd));

	if (ret != -EAFNOSUPPORT && ret != -EPROTONOSUPPORT && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(socket(protocol_family, socket_type, protocol));
}

COBALT_IMPL(int, close, (int fd))
{
	int oldtype;
	int ret;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	ret = XENOMAI_SKINCALL1(__rtdm_muxid, sc_rtdm_close, fd);

	pthread_setcanceltype(oldtype, NULL);

	if (ret != -EBADF && ret != -ENOSYS) {
		if (ret == 0)
			__STD(close(fd));
		return set_errno(ret);
	}

	return __STD(close(fd));
}

static int __xn_ioctl(int fd, unsigned long request, void *arg)
{
	int ret, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	ret = XENOMAI_SKINCALL3(__rtdm_muxid,
				sc_rtdm_ioctl,
				fd, request, arg);

	pthread_setcanceltype(oldtype, NULL);

	return ret;
}

COBALT_IMPL(int, ioctl, (int fd, unsigned long int request, ...))
{
	va_list ap;
	void *arg;
	int ret;

	va_start(ap, request);
	arg = va_arg(ap, void *);
	va_end(ap);

	ret = __xn_ioctl(fd, request, arg);
	if (ret != -EBADF && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(ioctl(fd, request, arg));
}

COBALT_IMPL(ssize_t, read, (int fd, void *buf, size_t nbyte))
{
	int ret, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	ret = XENOMAI_SKINCALL3(__rtdm_muxid,
				sc_rtdm_read,
				fd, buf, nbyte);

	pthread_setcanceltype(oldtype, NULL);

	if (ret != -EBADF && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(read(fd, buf, nbyte));
}

COBALT_IMPL(ssize_t, write, (int fd, const void *buf, size_t nbyte))
{
	int ret, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	ret = XENOMAI_SKINCALL3(__rtdm_muxid,
				sc_rtdm_write,
				fd, buf, nbyte);

	pthread_setcanceltype(oldtype, NULL);

	if (ret != -EBADF && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(write(fd, buf, nbyte));
}

static ssize_t __xn_recvmsg(int fd, struct msghdr *msg, int flags)
{
	int ret, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	ret = XENOMAI_SKINCALL3(__rtdm_muxid,
				sc_rtdm_recvmsg,
				fd, msg, flags);

	pthread_setcanceltype(oldtype, NULL);

	return ret;
}

COBALT_IMPL(ssize_t, recvmsg, (int fd, struct msghdr *msg, int flags))
{
	int ret;

	ret = __xn_recvmsg(fd, msg, flags);
	if (ret != -EBADF && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(recvmsg(fd, msg, flags));
}

static ssize_t __xn_sendmsg(int fd, const struct msghdr *msg, int flags)
{
	int ret, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	ret = XENOMAI_SKINCALL3(__rtdm_muxid,
				sc_rtdm_sendmsg,
				fd, msg, flags);

	pthread_setcanceltype(oldtype, NULL);

	return ret;
}

COBALT_IMPL(ssize_t, sendmsg, (int fd, const struct msghdr *msg, int flags))
{
	int ret;

	ret = __xn_sendmsg(fd, msg, flags);
	if (ret != -EBADF && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(sendmsg(fd, msg, flags));
}

COBALT_IMPL(ssize_t, recvfrom, (int fd, void *buf, size_t len, int flags,
				struct sockaddr *from, socklen_t *fromlen))
{
	struct iovec iov = { buf, len };
	struct msghdr msg =
		{ from, (from != NULL) ? *fromlen : 0, &iov, 1, NULL, 0 };
	int ret;

	ret = __xn_recvmsg(fd, &msg, flags);
	if (ret != -EBADF && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(recvfrom(fd, buf, len, flags, from, fromlen));
}

COBALT_IMPL(ssize_t, sendto, (int fd, const void *buf, size_t len, int flags,
			      const struct sockaddr *to, socklen_t tolen))
{
	struct iovec iov = { (void *)buf, len };
	struct msghdr msg =
		{ (struct sockaddr *)to, tolen, &iov, 1, NULL, 0 };
	int ret;

	ret = __xn_sendmsg(fd, &msg, flags);
	if (ret != -EBADF && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(sendto(fd, buf, len, flags, to, tolen));
}

COBALT_IMPL(ssize_t, recv, (int fd, void *buf, size_t len, int flags))
{
	struct iovec iov = { buf, len };
	struct msghdr msg = { NULL, 0, &iov, 1, NULL, 0 };
	int ret;

	ret = __xn_recvmsg(fd, &msg, flags);
	if (ret != -EBADF && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(recv(fd, buf, len, flags));
}

COBALT_IMPL(ssize_t, send, (int fd, const void *buf, size_t len, int flags))
{
	struct iovec iov = { (void *)buf, len };
	struct msghdr msg = { NULL, 0, &iov, 1, NULL, 0 };
	int ret;

	ret = __xn_sendmsg(fd, &msg, flags);
	if (ret != -EBADF && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(send(fd, buf, len, flags));
}

COBALT_IMPL(int, getsockopt, (int fd, int level, int optname, void *optval,
			      socklen_t *optlen))
{
	struct _rtdm_getsockopt_args args = { level, optname, optval, optlen };
	int ret;

	ret = __xn_ioctl(fd, _RTIOC_GETSOCKOPT, &args);
	if (ret != -EBADF && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(getsockopt(fd, level, optname, optval, optlen));
}

COBALT_IMPL(int, setsockopt, (int fd, int level, int optname, const void *optval,
			      socklen_t optlen))
{
	struct _rtdm_setsockopt_args args = {
		level, optname, (void *)optval, optlen
	};
	int ret;

	ret = __xn_ioctl(fd, _RTIOC_SETSOCKOPT, &args);
	if (ret != -EBADF && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(setsockopt(fd, level, optname, optval, optlen));
}

COBALT_IMPL(int, bind, (int fd, const struct sockaddr *my_addr, socklen_t addrlen))
{
	struct _rtdm_setsockaddr_args args = { my_addr, addrlen };
	int ret;

	ret = __xn_ioctl(fd, _RTIOC_BIND, &args);
	if (ret != -EBADF && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(bind(fd, my_addr, addrlen));
}

COBALT_IMPL(int, connect, (int fd, const struct sockaddr *serv_addr, socklen_t addrlen))
{
	struct _rtdm_setsockaddr_args args = { serv_addr, addrlen };
	int ret;

	ret = __xn_ioctl(fd, _RTIOC_CONNECT, &args);
	if (ret != -EBADF && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(connect(fd, serv_addr, addrlen));
}

COBALT_IMPL(int, listen, (int fd, int backlog))
{
	int ret;

	ret = __xn_ioctl(fd, _RTIOC_LISTEN, (void *)(long)backlog);
	if (ret != -EBADF && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(listen(fd, backlog));
}

COBALT_IMPL(int, accept, (int fd, struct sockaddr *addr, socklen_t *addrlen))
{
	struct _rtdm_getsockaddr_args args = { addr, addrlen };
	int ret;

	ret = __xn_ioctl(fd, _RTIOC_ACCEPT, &args);
	if (ret != -EBADF && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(accept(fd, addr, addrlen));
}

COBALT_IMPL(int, getsockname, (int fd, struct sockaddr *name, socklen_t *namelen))
{
	struct _rtdm_getsockaddr_args args = { name, namelen };
	int ret;

	ret = __xn_ioctl(fd, _RTIOC_GETSOCKNAME, &args);
	if (ret != -EBADF && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(getsockname(fd, name, namelen));
}

COBALT_IMPL(int, getpeername, (int fd, struct sockaddr *name, socklen_t *namelen))
{
	struct _rtdm_getsockaddr_args args = { name, namelen };
	int ret;

	ret = __xn_ioctl(fd, _RTIOC_GETPEERNAME, &args);
	if (ret != -EBADF && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(getpeername(fd, name, namelen));
}

COBALT_IMPL(int, shutdown, (int fd, int how))
{
	int ret;

	ret = __xn_ioctl(fd, _RTIOC_SHUTDOWN, (void *)(long)how);
	if (ret != -EBADF && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(shutdown(fd, how));
}

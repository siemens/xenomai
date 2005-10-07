/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#ifndef _XENO_POSIX_PTHREAD_H
#define _XENO_POSIX_PTHREAD_H

#include <sys/time.h>
#include <sys/socket.h>
#include <signal.h>
#include <time.h>
#include_next <pthread.h>
#include <nucleus/thread.h>
#include <nucleus/intr.h>

union __xeno_mutex {
    pthread_mutex_t native_mutex;
    struct __shadow_mutex {
#define SHADOW_MUTEX_MAGIC 0x0d140518
	unsigned magic;
	unsigned long handle;
    } shadow_mutex;
};

union __xeno_cond {
    pthread_cond_t native_cond;
    struct __shadow_cond {
#define SHADOW_COND_MAGIC 0x030f0e04
	unsigned magic;
	unsigned long handle;
    } shadow_cond;
};

struct timespec;

typedef unsigned long pthread_intr_t;

#ifndef CLOCK_MONOTONIC
/* Some archs do not implement this, but Xenomai always does. */
#define CLOCK_MONOTONIC 1
#endif /* CLOCK_MONOTONIC */

#define PTHREAD_SHIELD  XNSHIELD
#define PTHREAD_WARNSW  XNTRAPSW
#define PTHREAD_PRIMARY XNTHREAD_SPARE1

#define PTHREAD_IAUTOENA    XN_ISR_ENABLE
#define PTHREAD_IPROPAGATE  XN_ISR_CHAINED

#define PTHREAD_IENABLE     0
#define PTHREAD_IDISABLE    1

#ifdef __cplusplus
extern "C" {
#endif

int pthread_make_periodic_np(pthread_t thread,
			     struct timespec *starttp,
			     struct timespec *periodtp);
int pthread_wait_np(void);

int pthread_set_mode_np(int clrmask,
			int setmask);

int pthread_set_name_np(pthread_t thread,
			const char *name);

int pthread_intr_attach_np(pthread_intr_t *intr,
			   unsigned irq,
			   int mode);

int pthread_intr_detach_np(pthread_intr_t intr);

int pthread_intr_wait_np(pthread_intr_t intr,
			 const struct timespec *to);

int pthread_intr_control_np(pthread_intr_t intr,
			    int cmd);

int __real_pthread_create(pthread_t *tid,
			  const pthread_attr_t *attr,
			  void *(*start) (void *),
			  void *arg);

int __real_pthread_detach(pthread_t thread);

int __real_pthread_setschedparam(pthread_t thread,
				 int policy,
				 const struct sched_param *param);
int __real_sched_yield(void);

int __real_pthread_yield(void);

int __real_pthread_mutex_init(pthread_mutex_t *mutex,
			      const pthread_mutexattr_t *attr);

int __real_pthread_mutex_destroy(pthread_mutex_t *mutex);

int __real_pthread_mutex_destroy(pthread_mutex_t *mutex);

int __real_pthread_mutex_lock(pthread_mutex_t *mutex);

int __real_pthread_mutex_timedlock(pthread_mutex_t *mutex,
				   const struct timespec *to);

int __real_pthread_mutex_trylock(pthread_mutex_t *mutex);

int __real_pthread_mutex_unlock(pthread_mutex_t *mutex);

int __real_pthread_cond_init (pthread_cond_t *cond,
			      const pthread_condattr_t *attr);

int __real_pthread_cond_destroy(pthread_cond_t *cond);

int __real_pthread_cond_wait(pthread_cond_t *cond,
			     pthread_mutex_t *mutex);

int __real_pthread_cond_timedwait(pthread_cond_t *cond,
				  pthread_mutex_t *mutex,
				  const struct timespec *abstime);

int __real_pthread_cond_signal(pthread_cond_t *cond);

int __real_pthread_cond_broadcast(pthread_cond_t *cond);

int __real_clock_getres(clockid_t clock_id,
			struct timespec *tp);

int __real_clock_gettime(clockid_t clock_id,
			 struct timespec *tp);

int __real_clock_settime(clockid_t clock_id,
			 const struct timespec *tp);

int __real_clock_nanosleep(clockid_t clock_id,
			   int flags,
			   const struct timespec *rqtp,
			   struct timespec *rmtp);

int __real_nanosleep(const struct timespec *rqtp,
		     struct timespec *rmtp);

int __real_timer_create (clockid_t clockid,
			 struct sigevent *evp,
			 timer_t *timerid);

int __real_timer_delete (timer_t timerid);

int __real_timer_settime(timer_t timerid,
			 int flags,
			 const struct itimerspec *value,
			 struct itimerspec *ovalue);

int __real_timer_gettime(timer_t timerid,
			 struct itimerspec *value);

int __real_timer_getoverrun(timer_t timerid);

int __real_open(const char *path, int oflag, ...);

int __real_socket(int protocol_family, int socket_type, int protocol);

int __real_close(int fd);

int __real_ioctl(int fd, int request, ...);

ssize_t __real_read(int fd, void *buf, size_t nbyte);

ssize_t __real_write(int fd, const void *buf, size_t nbyte);

ssize_t __real_recvmsg(int fd, struct msghdr *msg, int flags);

ssize_t __real_sendmsg(int fd, const struct msghdr *msg, int flags);

ssize_t __real_recvfrom(int fd, void *buf, size_t len, int flags,
                        struct sockaddr *from, socklen_t *fromlen);

ssize_t __real_sendto(int fd, const void *buf, size_t len, int flags,
                      const struct sockaddr *to, socklen_t tolen);

ssize_t __real_recv(int fd, void *buf, size_t len, int flags);

ssize_t __real_send(int fd, const void *buf, size_t len, int flags);

int __real_getsockopt(int fd, int level, int optname, void *optval,
                      socklen_t *optlen);

int __real_setsockopt(int fd, int level, int optname, const void *optval,
                      socklen_t optlen);

int __real_bind(int fd, const struct sockaddr *my_addr, socklen_t addrlen);

int __real_connect(int fd, const struct sockaddr *serv_addr,
                   socklen_t addrlen);

int __real_listen(int fd, int backlog);

int __real_accept(int fd, struct sockaddr *addr, socklen_t *addrlen);

int __real_getsockname(int fd, struct sockaddr *name, socklen_t *namelen);

int __real_getpeername(int fd, struct sockaddr *name, socklen_t *namelen);

int __real_shutdown(int fd, int how);

#ifdef __cplusplus
}
#endif

#endif /* _XENO_POSIX_PTHREAD_H */

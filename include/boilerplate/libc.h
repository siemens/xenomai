/*
 * Copyright (C) 2014 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _BOILERPLATE_LIBC_H
#define _BOILERPLATE_LIBC_H

#ifdef __IN_XENO__
/*
 * Quirks for dealing with outdated libc* issues.  This header will be
 * parsed by the Xenomai implementation only, applications based on it
 * have to provide their own set of wrappers as they should decide by
 * themselves what to do when a feature is missing.
 */
#include <xeno_config.h>
#include <errno.h>
#include <boilerplate/compiler.h>

#if defined(__UCLIBC__) && !defined(UCLIBC_HAS_THREADS_NATIVE)
enum {
	PTHREAD_PRIO_NONE,
	PTHREAD_PRIO_INHERIT,
	PTHREAD_PRIO_PROTECT
};
#endif /* __UCLIBC__  && !UCLIBC_HAS_THREADS_NATIVE */

#ifndef HAVE_FORK
static inline int fork(void)
{
	errno = ENOSYS;
	return -1;
}
#endif

#ifndef HAVE_PTHREAD_ATFORK
#ifndef HAVE_FORK
static inline
int pthread_atfork(void (*prepare)(void), void (*parent)(void),
		   void (*child)(void))
{
	return 0;
}
#else
#error "fork() without pthread_atfork()"
#endif
#endif /* !HAVE_PTHREAD_ATFORK */

#ifndef HAVE_PTHREAD_GETATTR_NP
static inline
int pthread_getattr_np(pthread_t th, pthread_attr_t *attr)
{
	return ENOSYS;
}
#endif /* !HAVE_PTHREAD_GETATTR_NP */

#ifndef HAVE_PTHREAD_CONDATTR_SETCLOCK
static inline
int pthread_condattr_setclock(pthread_condattr_t *__restrict__ attr,
			      clockid_t clock_id)
{
	return clock_id == CLOCK_REALTIME ? 0 : ENOSYS;
}
#endif	/* !HAVE_PTHREAD_CONDATTR_SETCLOCK */

#ifndef HAVE_PTHREAD_CONDATTR_GETCLOCK
static inline
int pthread_condattr_getclock(const pthread_condattr_t *__restrict__ attr,
			      clockid_t *__restrict__ clock_id)
{
	*clock_id = CLOCK_REALTIME;

	return 0;
}
#endif	/* !HAVE_PTHREAD_CONDATTR_GETCLOCK */

#ifndef HAVE_PTHREAD_MUTEXATTR_SETPROTOCOL
static inline
int pthread_mutexattr_setprotocol(pthread_mutexattr_t *__restrict__ attr,
				  int protocol)
{
	return protocol == PTHREAD_PRIO_NONE ? 0 : ENOSYS;
}
#endif /* !HAVE_PTHREAD_MUTEXATTR_SETPROTOCOL */

#ifndef HAVE_PTHREAD_MUTEXATTR_GETPROTOCOL
static inline
int pthread_mutexattr_getprotocol(const pthread_mutexattr_t *
				  __restrict__ attr, int *__restrict__ protocol)
{
	*protocol = PTHREAD_PRIO_NONE;

	return 0;
}
#endif /* !HAVE_PTHREAD_MUTEXATTR_GETPROTOCOL */

#ifndef HAVE_PTHREAD_ATTR_SETAFFINITY_NP
static inline
int pthread_attr_setaffinity_np(pthread_attr_t *attr,
				size_t cpusetsize, const cpu_set_t *cpuset)
{
	return ENOSYS;
}
#endif /* !HAVE_PTHREAD_ATTR_SETAFFINITY_NP */

#if !defined(HAVE_CLOCK_NANOSLEEP) && defined(CONFIG_XENO_MERCURY)
/*
 * Best effort for a Mercury setup based on an outdated libc lacking
 * "advanced" real-time support.  Too bad if the system clock is set
 * during sleep time. This is a non-issue for Cobalt, as the libcobalt
 * implementation will always be picked instead.
 */
__weak inline int clock_nanosleep(clockid_t clock_id, int flags,
				  const struct timespec *request,
				  struct timespec *remain)
{
	struct timespec now, tmp;

	tmp = *request;
	if (flags) {
		clock_gettime(CLOCK_REALTIME, &now);
		tmp.tv_sec -= now.tv_sec;
		tmp.tv_nsec -= now.tv_nsec;
		if (tmp.tv_nsec < 0) {
			tmp.tv_sec--;
			tmp.tv_nsec += 1000000000;
		}
	}

	return nanosleep(&tmp, remain);
}
#else  /* HAVE_CLOCK_NANOSLEEP || COBALT */
/*
 * Either libcobalt or the libc implements this, we only want the
 * possibly missing declaration from the libc headers.
 */
int clock_nanosleep(clockid_t clock_id, int flags,
		    const struct timespec *request,
		    struct timespec *remain);
#endif /* HAVE_CLOCK_NANOSLEEP || COBALT */

#ifndef HAVE_SCHED_GETCPU
/*
 * Might be declared in uClibc headers but not actually implemented,
 * so we make the placeholder a weak symbol.
 */
__weak inline int sched_getcpu(void)
{
	return 0;   /* outdated uClibc: assume uniprocessor. */
}
#endif /* !HAVE_SCHED_GETCPU */

#ifndef HAVE_SHM_OPEN
__weak inline int shm_open(const char *name, int oflag, mode_t mode)
{
	errno = ENOSYS;
	return -1;
}
#endif	/* !HAVE_SHM_OPEN */

#ifndef HAVE_SHM_UNLINK
__weak inline int shm_unlink(const char *name)
{
	errno = ENOSYS;
	return -1;
}
#endif	/* !HAVE_SHM_UNLINK */

#ifndef HAVE_PTHREAD_MUTEXATTR_SETROBUST_NP
#define pthread_mutexattr_setrobust_np(__attr, __robust)	\
	({ ENOSYS; })
#endif /* !HAVE_PTHREAD_MUTEXATTR_SETROBUST_NP */

#if !defined(HAVE_PTHREAD_SETNAME_NP) && defined(CONFIG_XENO_MERCURY)
static inline
int pthread_setname_np(pthread_t thread, const char *name)
{
	return ENOSYS;
}
#else /* HAVE_PTHREAD_SETNAME_NP || COBALT */
/* Same as clock_nanosleep() */
int pthread_setname_np(pthread_t thread, const char *name);
#endif /* HAVE_PTHREAD_SETNAME_NP || COBALT */

#endif /* __IN_XENO__ */

#endif /* _BOILERPLATE_LIBC_H */

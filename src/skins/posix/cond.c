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

#include <errno.h>
#include <posix/syscall.h>
#include <pthread.h>

extern int __pse51_muxid;

int __wrap_pthread_condattr_init(pthread_condattr_t *attr)
{
	return -XENOMAI_SKINCALL1(__pse51_muxid, __pse51_condattr_init, attr);
}

int __wrap_pthread_condattr_destroy(pthread_condattr_t *attr)
{
	return -XENOMAI_SKINCALL1(__pse51_muxid,__pse51_condattr_destroy,attr);
}

int __wrap_pthread_condattr_getclock(const pthread_condattr_t *attr,
				     clockid_t *clk_id)
{
	return -XENOMAI_SKINCALL2(__pse51_muxid,
				  __pse51_condattr_getclock, attr, clk_id);
}

int __wrap_pthread_condattr_setclock(pthread_condattr_t *attr,
				     clockid_t clk_id)
{
	return -XENOMAI_SKINCALL2(__pse51_muxid,
				  __pse51_condattr_setclock, attr, clk_id);
}

int __wrap_pthread_condattr_getpshared(const pthread_condattr_t *attr,
				       int *pshared)
{
	return -XENOMAI_SKINCALL2(__pse51_muxid,
				  __pse51_condattr_getpshared, attr, pshared);
}

int __wrap_pthread_condattr_setpshared(pthread_condattr_t *attr, int pshared)
{
	return -XENOMAI_SKINCALL2(__pse51_muxid,
				  __pse51_condattr_setpshared, attr, pshared);
}

int __wrap_pthread_cond_init(pthread_cond_t * cond,
			     const pthread_condattr_t * attr)
{
	union __xeno_cond *_cond = (union __xeno_cond *)cond;
	int err;

	err = -XENOMAI_SKINCALL2(__pse51_muxid,
				 __pse51_cond_init, &_cond->shadow_cond, attr);
	return err;
}

int __wrap_pthread_cond_destroy(pthread_cond_t * cond)
{
	union __xeno_cond *_cond = (union __xeno_cond *)cond;

	return -XENOMAI_SKINCALL1(__pse51_muxid,
				  __pse51_cond_destroy, &_cond->shadow_cond);
}

int __wrap_pthread_cond_wait(pthread_cond_t * cond, pthread_mutex_t * mutex)
{
	union __xeno_mutex *_mutex = (union __xeno_mutex *)mutex;
	union __xeno_cond *_cond = (union __xeno_cond *)cond;
	int err, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	err = XENOMAI_SKINCALL2(__pse51_muxid,
				__pse51_cond_wait,
				&_cond->shadow_cond, &_mutex->shadow_mutex);

	pthread_setcanceltype(oldtype, NULL);

	return err == -EINTR ? 0 : -err;
}

int __wrap_pthread_cond_timedwait(pthread_cond_t * cond,
				  pthread_mutex_t * mutex,
				  const struct timespec *abstime)
{
	union __xeno_mutex *_mutex = (union __xeno_mutex *)mutex;
	union __xeno_cond *_cond = (union __xeno_cond *)cond;
	int err, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	err = XENOMAI_SKINCALL3(__pse51_muxid,
				__pse51_cond_timedwait,
				&_cond->shadow_cond,
				&_mutex->shadow_mutex, abstime);

	pthread_setcanceltype(oldtype, NULL);

	return err == -EINTR ? 0 : -err;
}

int __wrap_pthread_cond_signal(pthread_cond_t * cond)
{
	union __xeno_cond *_cond = (union __xeno_cond *)cond;

	return -XENOMAI_SKINCALL1(__pse51_muxid,
				  __pse51_cond_signal, &_cond->shadow_cond);
}

int __wrap_pthread_cond_broadcast(pthread_cond_t * cond)
{
	union __xeno_cond *_cond = (union __xeno_cond *)cond;

	return -XENOMAI_SKINCALL1(__pse51_muxid,
				  __pse51_cond_broadcast, &_cond->shadow_cond);
}

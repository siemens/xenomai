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
#include <pthread.h>
#include <limits.h>
#include <nucleus/synch.h>
#include <cobalt/syscall.h>
#include <kernel/cobalt/mutex.h>
#include <asm-generic/bits/current.h>
#include "internal.h"

int __wrap_pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
	return -XENOMAI_SKINCALL1(__cobalt_muxid, sc_cobalt_mutexattr_init, attr);
}

int __wrap_pthread_mutexattr_destroy(pthread_mutexattr_t *attr)
{
	return -XENOMAI_SKINCALL1(__cobalt_muxid,sc_cobalt_mutexattr_destroy,attr);
}

int __wrap_pthread_mutexattr_gettype(const pthread_mutexattr_t *attr,
				     int *type)
{
	return -XENOMAI_SKINCALL2(__cobalt_muxid,
				  sc_cobalt_mutexattr_gettype, attr, type);
}

int __wrap_pthread_mutexattr_settype(pthread_mutexattr_t *attr,
				     int type)
{
	return -XENOMAI_SKINCALL2(__cobalt_muxid,
				  sc_cobalt_mutexattr_settype, attr, type);
}

int __wrap_pthread_mutexattr_getprotocol(const pthread_mutexattr_t *attr,
					 int *proto)
{
	return -XENOMAI_SKINCALL2(__cobalt_muxid,
				  sc_cobalt_mutexattr_getprotocol, attr, proto);
}

int __wrap_pthread_mutexattr_setprotocol(pthread_mutexattr_t *attr,
					 int proto)
{
	return -XENOMAI_SKINCALL2(__cobalt_muxid,
				  sc_cobalt_mutexattr_setprotocol, attr, proto);
}

int __wrap_pthread_mutexattr_getpshared(const pthread_mutexattr_t *attr,
					int *pshared)
{
	return -XENOMAI_SKINCALL2(__cobalt_muxid,
				  sc_cobalt_mutexattr_getpshared, attr, pshared);
}

int __wrap_pthread_mutexattr_setpshared(pthread_mutexattr_t *attr, int pshared)
{
	return -XENOMAI_SKINCALL2(__cobalt_muxid,
				  sc_cobalt_mutexattr_setpshared, attr, pshared);
}

int __wrap_pthread_mutex_init(pthread_mutex_t *mutex,
			      const pthread_mutexattr_t *attr)
{
	struct __shadow_mutex *_mutex =
		&((union __xeno_mutex *)mutex)->shadow_mutex;
	int err;

	if (_mutex->magic == COBALT_MUTEX_MAGIC) {
		err = -XENOMAI_SKINCALL1(__cobalt_muxid,
					 sc_cobalt_check_init,_mutex);

		if (err)
			return err;
	}

	err = -XENOMAI_SKINCALL2(__cobalt_muxid,sc_cobalt_mutex_init,_mutex,attr);

	if (!_mutex->attr.pshared)
		_mutex->dat = (struct mutex_dat *)
			(xeno_sem_heap[0] + _mutex->dat_offset);

	return err;
}

int __wrap_pthread_mutex_destroy(pthread_mutex_t *mutex)
{
	struct __shadow_mutex *_mutex =
		&((union __xeno_mutex *)mutex)->shadow_mutex;
	int err;

	if (_mutex->magic != COBALT_MUTEX_MAGIC)
		return EINVAL;

	err = XENOMAI_SKINCALL1(__cobalt_muxid, sc_cobalt_mutex_destroy, _mutex);

	return -err;
}

int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex)
{
	struct __shadow_mutex *_mutex =
		&((union __xeno_mutex *)mutex)->shadow_mutex;
	unsigned long status;
	xnhandle_t cur;
	int err;

	cur = xeno_get_current();
	if (cur == XN_NO_HANDLE)
		return EPERM;

	if (_mutex->magic != COBALT_MUTEX_MAGIC)
		return EINVAL;

	/*
	 * We track resource ownership for non real-time shadows in
	 * order to handle the auto-relax feature, so we must always
	 * obtain them via a syscall.
	 */
	status = xeno_get_current_mode();
	if (likely(!(status & (XNRELAX|XNOTHER)))) {
		err = xnsynch_fast_acquire(mutex_get_ownerp(_mutex), cur);
		if (likely(!err)) {
			_mutex->lockcnt = 1;
			return 0;
		}
	} else {
		err = xnsynch_fast_owner_check(mutex_get_ownerp(_mutex), cur);
		if (!err)
			err = -EBUSY;
	}

	if (err == -EBUSY)
		switch(_mutex->attr.type) {
		case PTHREAD_MUTEX_NORMAL:
			break;

		case PTHREAD_MUTEX_ERRORCHECK:
			return EDEADLK;

		case PTHREAD_MUTEX_RECURSIVE:
			if (_mutex->lockcnt == UINT_MAX)
				return EAGAIN;
			++_mutex->lockcnt;
			return 0;
		}

	do {
		err = XENOMAI_SKINCALL1(__cobalt_muxid,sc_cobalt_mutex_lock,_mutex);
	} while (err == -EINTR);

	if (!err)
		_mutex->lockcnt = 1;

	return -err;
}

int __wrap_pthread_mutex_timedlock(pthread_mutex_t *mutex,
				   const struct timespec *to)
{
	struct __shadow_mutex *_mutex =
		&((union __xeno_mutex *)mutex)->shadow_mutex;
	unsigned long status;
	xnhandle_t cur;
	int err;

	cur = xeno_get_current();
	if (cur == XN_NO_HANDLE)
		return EPERM;

	if (_mutex->magic != COBALT_MUTEX_MAGIC)
		return EINVAL;

	/* See __wrap_pthread_mutex_lock() */
	status = xeno_get_current_mode();
	if (likely(!(status & (XNRELAX|XNOTHER)))) {
		err = xnsynch_fast_acquire(mutex_get_ownerp(_mutex), cur);
		if (likely(!err)) {
			_mutex->lockcnt = 1;
			return 0;
		}
	} else {
		err = xnsynch_fast_owner_check(mutex_get_ownerp(_mutex), cur);
		if (!err)
			err = -EBUSY;
	}

	if (err == -EBUSY)
		switch(_mutex->attr.type) {
		case PTHREAD_MUTEX_NORMAL:
			break;

		case PTHREAD_MUTEX_ERRORCHECK:
			return EDEADLK;

		case PTHREAD_MUTEX_RECURSIVE:
			if (_mutex->lockcnt == UINT_MAX)
				return EAGAIN;

			++_mutex->lockcnt;
			return 0;
		}

	do {
		err = XENOMAI_SKINCALL2(__cobalt_muxid,
					sc_cobalt_mutex_timedlock, _mutex, to);
	} while (err == -EINTR);

	if (!err)
		_mutex->lockcnt = 1;
	return -err;
}

int __wrap_pthread_mutex_trylock(pthread_mutex_t *mutex)
{
	struct __shadow_mutex *_mutex =
		&((union __xeno_mutex *)mutex)->shadow_mutex;
	unsigned long status;
	struct timespec ts;
	xnhandle_t cur;
	int err;

	cur = xeno_get_current();
	if (cur == XN_NO_HANDLE)
		return EPERM;

	if (unlikely(_mutex->magic != COBALT_MUTEX_MAGIC))
		return EINVAL;

	status = xeno_get_current_mode();
	if (likely((status & (XNRELAX|XNOTHER)) == 0)) {
		err = xnsynch_fast_acquire(mutex_get_ownerp(_mutex), cur);
		if (likely(err == 0)) {
			_mutex->lockcnt = 1;
			return 0;
		}
	} else {
		err = xnsynch_fast_owner_check(mutex_get_ownerp(_mutex), cur);
		if (err < 0)
			goto do_syscall;

		err = -EBUSY;
	}

	if (err == -EBUSY && _mutex->attr.type == PTHREAD_MUTEX_RECURSIVE) {
		if (_mutex->lockcnt == UINT_MAX)
			return EAGAIN;

		++_mutex->lockcnt;
		return 0;
	}

	return EBUSY;

do_syscall:

	__RT(clock_gettime(CLOCK_REALTIME, &ts));
	do {
		err = XENOMAI_SKINCALL2(__cobalt_muxid,
					sc_cobalt_mutex_timedlock, _mutex, &ts);
	} while (err == -EINTR);

	if (err) {
		if (err == -ETIMEDOUT || err == -EDEADLK)
			return EBUSY;
		return -err;
	}

	_mutex->lockcnt = 1;

	return 0;
}

int __wrap_pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	struct __shadow_mutex *_mutex =
		&((union __xeno_mutex *)mutex)->shadow_mutex;
	struct mutex_dat *datp = NULL;
	xnhandle_t cur = XN_NO_HANDLE;
	int err;

	if (unlikely(_mutex->magic != COBALT_MUTEX_MAGIC))
		return EINVAL;

	cur = xeno_get_current();
	if (cur == XN_NO_HANDLE)
		return EPERM;

	datp = mutex_get_datp(_mutex);
	if (xnsynch_fast_owner_check(&datp->owner, cur) != 0)
		return EPERM;

	if (_mutex->lockcnt > 1) {
		--_mutex->lockcnt;
		return 0;
	}

	if ((datp->flags & COBALT_MUTEX_COND_SIGNAL))
		goto do_syscall;

	if (unlikely(xeno_get_current_mode() & XNOTHER))
		goto do_syscall;

	if (likely(xnsynch_fast_release(&datp->owner, cur)))
		return 0;
do_syscall:

	do {
		err = XENOMAI_SKINCALL1(__cobalt_muxid,
					sc_cobalt_mutex_unlock, _mutex);
	} while (err == -EINTR);

	return -err;
}

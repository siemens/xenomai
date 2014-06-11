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
#include <asm/xenomai/syscall.h>
#include "current.h"
#include "internal.h"

/**
 * @ingroup cobalt
 * @defgroup cobalt_mutex Mutual exclusion
 *
 * Cobalt/POSIX mutual exclusion services
 *
 * A mutex is a MUTual EXclusion device, and is useful for protecting
 * shared data structures from concurrent modifications, and implementing
 * critical sections and monitors.
 *
 * A mutex has two possible states: unlocked (not owned by any thread), and
 * locked (owned by one thread). A mutex can never be owned by two different
 * threads simultaneously. A thread attempting to lock a mutex that is already
 * locked by another thread is suspended until the owning thread unlocks the
 * mutex first.
 *
 * Before it can be used, a mutex has to be initialized with
 * pthread_mutex_init(). An attribute object, which reference may be passed to
 * this service, allows to select the features of the created mutex, namely its
 * @a type (see pthread_mutexattr_settype()), the priority @a protocol it
 * uses (see pthread_mutexattr_setprotocol()) and whether it may be shared
 * between several processes (see pthread_mutexattr_setpshared()).
 *
 * By default, Cobalt mutexes are of the normal type, use no
 * priority protocol and may not be shared between several processes.
 *
 * Note that only pthread_mutex_init() may be used to initialize a mutex, using
 * the static initializer @a PTHREAD_MUTEX_INITIALIZER is not supported.
 *
 *@{
 */

static pthread_mutexattr_t cobalt_default_mutexattr;

void cobalt_default_mutexattr_init(void)
{
	pthread_mutexattr_init(&cobalt_default_mutexattr);
}

COBALT_IMPL(int, pthread_mutex_init, (pthread_mutex_t *mutex,
				      const pthread_mutexattr_t *attr))
{
	struct cobalt_mutex_shadow *_mutex =
		&((union cobalt_mutex_union *)mutex)->shadow_mutex;
	struct cobalt_mutexattr kmattr;
	struct mutex_dat *datp;
	int err, tmp;

	if (_mutex->magic == COBALT_MUTEX_MAGIC) {
		err = -XENOMAI_SKINCALL1(__cobalt_muxid,
					 sc_cobalt_mutex_check_init,_mutex);

		if (err)
			return err;
	}

	if (attr == NULL)
		attr = &cobalt_default_mutexattr;

	err = pthread_mutexattr_getpshared(attr, &tmp);
	if (err)
		return err;
	kmattr.pshared = tmp;

	err = pthread_mutexattr_gettype(attr, &tmp);
	if (err)
		return err;
	kmattr.type = tmp;

	err = pthread_mutexattr_getprotocol(attr, &tmp);
	if (err)
		return err;
	if (tmp == PTHREAD_PRIO_PROTECT) { /* Prio ceiling unsupported */
		err = EINVAL;
		return err;
	}
	kmattr.protocol = tmp;

	err = -XENOMAI_SKINCALL2(__cobalt_muxid,sc_cobalt_mutex_init,_mutex,&kmattr);
	if (err)
		return err;

	if (!_mutex->attr.pshared) {
		datp = (struct mutex_dat *)
			(cobalt_sem_heap[0] + _mutex->dat_offset);
		_mutex->dat = datp;
	} else
		datp = mutex_get_datp(_mutex);

	__cobalt_prefault(datp);

	return err;
}

COBALT_IMPL(int, pthread_mutex_destroy, (pthread_mutex_t *mutex))
{
	struct cobalt_mutex_shadow *_mutex =
		&((union cobalt_mutex_union *)mutex)->shadow_mutex;
	int err;

	if (_mutex->magic != COBALT_MUTEX_MAGIC)
		return EINVAL;

	err = XENOMAI_SKINCALL1(__cobalt_muxid, sc_cobalt_mutex_destroy, _mutex);

	return -err;
}

COBALT_IMPL(int, pthread_mutex_lock, (pthread_mutex_t *mutex))
{
	struct cobalt_mutex_shadow *_mutex =
		&((union cobalt_mutex_union *)mutex)->shadow_mutex;
	unsigned long status;
	xnhandle_t cur;
	int err;

	cur = cobalt_get_current();
	if (cur == XN_NO_HANDLE)
		return EPERM;

	if (_mutex->magic != COBALT_MUTEX_MAGIC)
		return EINVAL;

	/*
	 * We track resource ownership for non real-time shadows in
	 * order to handle the auto-relax feature, so we must always
	 * obtain them via a syscall.
	 */
	status = cobalt_get_current_mode();
	if ((status & (XNRELAX|XNWEAK)) == 0) {
		err = xnsynch_fast_acquire(mutex_get_ownerp(_mutex), cur);
		if (err == 0) {
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

COBALT_IMPL(int, pthread_mutex_timedlock, (pthread_mutex_t *mutex,
					   const struct timespec *to))
{
	struct cobalt_mutex_shadow *_mutex =
		&((union cobalt_mutex_union *)mutex)->shadow_mutex;
	unsigned long status;
	xnhandle_t cur;
	int err;

	cur = cobalt_get_current();
	if (cur == XN_NO_HANDLE)
		return EPERM;

	if (_mutex->magic != COBALT_MUTEX_MAGIC)
		return EINVAL;

	/* See __cobalt_pthread_mutex_lock() */
	status = cobalt_get_current_mode();
	if ((status & (XNRELAX|XNWEAK)) == 0) {
		err = xnsynch_fast_acquire(mutex_get_ownerp(_mutex), cur);
		if (err == 0) {
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

COBALT_IMPL(int, pthread_mutex_trylock, (pthread_mutex_t *mutex))
{
	struct cobalt_mutex_shadow *_mutex =
		&((union cobalt_mutex_union *)mutex)->shadow_mutex;
	unsigned long status;
	xnhandle_t cur;
	int err;

	cur = cobalt_get_current();
	if (cur == XN_NO_HANDLE)
		return EPERM;

	if (_mutex->magic != COBALT_MUTEX_MAGIC)
		return EINVAL;

	status = cobalt_get_current_mode();
	if ((status & (XNRELAX|XNWEAK)) == 0) {
		err = xnsynch_fast_acquire(mutex_get_ownerp(_mutex), cur);
		if (err == 0) {
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

	do {
		err = XENOMAI_SKINCALL1(__cobalt_muxid,
					sc_cobalt_mutex_trylock, _mutex);
	} while (err == -EINTR);

	if (!err)
		_mutex->lockcnt = 1;

	return -err;
}

COBALT_IMPL(int, pthread_mutex_unlock, (pthread_mutex_t *mutex))
{
	struct cobalt_mutex_shadow *_mutex =
		&((union cobalt_mutex_union *)mutex)->shadow_mutex;
	struct mutex_dat *datp = NULL;
	xnhandle_t cur = XN_NO_HANDLE;
	int err;

	if (_mutex->magic != COBALT_MUTEX_MAGIC)
		return EINVAL;

	cur = cobalt_get_current();
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

	if (cobalt_get_current_mode() & XNWEAK)
		goto do_syscall;

	if (xnsynch_fast_release(&datp->owner, cur))
		return 0;
do_syscall:

	do {
		err = XENOMAI_SKINCALL1(__cobalt_muxid,
					sc_cobalt_mutex_unlock, _mutex);
	} while (err == -EINTR);

	return -err;
}

/** @} */

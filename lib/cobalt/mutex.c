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

extern int __cobalt_muxid;

#define COBALT_MUTEX_MAGIC (0x86860303)

extern unsigned long xeno_sem_heap[2];

static xnarch_atomic_t *get_ownerp(struct __shadow_mutex *shadow)
{
	if (likely(!shadow->attr.pshared))
		return shadow->owner;

	return (xnarch_atomic_t *)(xeno_sem_heap[1] + shadow->owner_offset);
}

int __wrap_pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
	return -XENOMAI_SKINCALL1(__cobalt_muxid, __cobalt_mutexattr_init, attr);
}

int __wrap_pthread_mutexattr_destroy(pthread_mutexattr_t *attr)
{
	return -XENOMAI_SKINCALL1(__cobalt_muxid,__cobalt_mutexattr_destroy,attr);
}

int __wrap_pthread_mutexattr_gettype(const pthread_mutexattr_t *attr,
				     int *type)
{
	return -XENOMAI_SKINCALL2(__cobalt_muxid,
				  __cobalt_mutexattr_gettype, attr, type);
}

int __wrap_pthread_mutexattr_settype(pthread_mutexattr_t *attr,
				     int type)
{
	return -XENOMAI_SKINCALL2(__cobalt_muxid,
				  __cobalt_mutexattr_settype, attr, type);
}

int __wrap_pthread_mutexattr_getprotocol(const pthread_mutexattr_t *attr,
					 int *proto)
{
	return -XENOMAI_SKINCALL2(__cobalt_muxid,
				  __cobalt_mutexattr_getprotocol, attr, proto);
}

int __wrap_pthread_mutexattr_setprotocol(pthread_mutexattr_t *attr,
					 int proto)
{
	return -XENOMAI_SKINCALL2(__cobalt_muxid,
				  __cobalt_mutexattr_setprotocol, attr, proto);
}

int __wrap_pthread_mutexattr_getpshared(const pthread_mutexattr_t *attr,
					int *pshared)
{
	return -XENOMAI_SKINCALL2(__cobalt_muxid,
				  __cobalt_mutexattr_getpshared, attr, pshared);
}

int __wrap_pthread_mutexattr_setpshared(pthread_mutexattr_t *attr, int pshared)
{
	return -XENOMAI_SKINCALL2(__cobalt_muxid,
				  __cobalt_mutexattr_setpshared, attr, pshared);
}

int __wrap_pthread_mutex_init(pthread_mutex_t *mutex,
			      const pthread_mutexattr_t *attr)
{
	union __xeno_mutex *_mutex = (union __xeno_mutex *)mutex;
	struct __shadow_mutex *shadow = &_mutex->shadow_mutex;
	int err;

	if (shadow->magic == COBALT_MUTEX_MAGIC) {
		err = -XENOMAI_SKINCALL1(__cobalt_muxid,
					 __cobalt_check_init,shadow);

		if (err)
			return err;
	}

	err = -XENOMAI_SKINCALL2(__cobalt_muxid,__cobalt_mutex_init,shadow,attr);

	if (!shadow->attr.pshared)
		shadow->owner = (xnarch_atomic_t *)
			(xeno_sem_heap[0] + shadow->owner_offset);

	return err;
}

int __wrap_pthread_mutex_destroy(pthread_mutex_t *mutex)
{
	union __xeno_mutex *_mutex = (union __xeno_mutex *)mutex;
	struct __shadow_mutex *shadow = &_mutex->shadow_mutex;
	int err;

	if (shadow->magic != COBALT_MUTEX_MAGIC)
		return EINVAL;

	err = XENOMAI_SKINCALL1(__cobalt_muxid, __cobalt_mutex_destroy, shadow);

	return -err;
}

int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex)
{
	union __xeno_mutex *_mutex = (union __xeno_mutex *)mutex;
	struct __shadow_mutex *shadow = &_mutex->shadow_mutex;
	int err;

	unsigned long status;
	xnhandle_t cur;

	cur = xeno_get_current();
	if (cur == XN_NO_HANDLE)
		return EPERM;

	if (shadow->magic != COBALT_MUTEX_MAGIC)
		return EINVAL;

	/*
	 * We track resource ownership for non real-time shadows in
	 * order to handle the auto-relax feature, so we must always
	 * obtain them via a syscall.
	 */
	status = xeno_get_current_mode();
	if (likely(!(status & (XNRELAX|XNOTHER)))) {
		err = xnsynch_fast_acquire(get_ownerp(shadow), cur);
		if (likely(!err)) {
			shadow->lockcnt = 1;
			return 0;
		}
	} else {
		err = xnsynch_fast_owner_check(get_ownerp(shadow), cur);
		if (!err)
			err = -EBUSY;
	}

	if (err == -EBUSY)
		switch(shadow->attr.type) {
		case PTHREAD_MUTEX_NORMAL:
			break;

		case PTHREAD_MUTEX_ERRORCHECK:
			return EDEADLK;

		case PTHREAD_MUTEX_RECURSIVE:
			if (shadow->lockcnt == UINT_MAX)
				return EAGAIN;
			++shadow->lockcnt;
			return 0;
		}

	do {
		err = XENOMAI_SKINCALL1(__cobalt_muxid,__cobalt_mutex_lock,shadow);
	} while (err == -EINTR);

	if (!err)
		shadow->lockcnt = 1;

	return -err;
}

int __wrap_pthread_mutex_timedlock(pthread_mutex_t *mutex,
				   const struct timespec *to)
{
	union __xeno_mutex *_mutex = (union __xeno_mutex *)mutex;
	struct __shadow_mutex *shadow = &_mutex->shadow_mutex;
	unsigned long status;
	xnhandle_t cur;
	int err;

	cur = xeno_get_current();
	if (cur == XN_NO_HANDLE)
		return EPERM;

	if (shadow->magic != COBALT_MUTEX_MAGIC)
		return EINVAL;

	/* See __wrap_pthread_mutex_lock() */
	status = xeno_get_current_mode();
	if (likely(!(status & (XNRELAX|XNOTHER)))) {
		err = xnsynch_fast_acquire(get_ownerp(shadow), cur);
		if (likely(!err)) {
			shadow->lockcnt = 1;
			return 0;
		}
	} else {
		err = xnsynch_fast_owner_check(get_ownerp(shadow), cur);
		if (!err)
			err = -EBUSY;
	}

	if (err == -EBUSY)
		switch(shadow->attr.type) {
		case PTHREAD_MUTEX_NORMAL:
			break;

		case PTHREAD_MUTEX_ERRORCHECK:
			return EDEADLK;

		case PTHREAD_MUTEX_RECURSIVE:
			if (shadow->lockcnt == UINT_MAX)
				return EAGAIN;

			++shadow->lockcnt;
			return 0;
		}

	do {
		err = XENOMAI_SKINCALL2(__cobalt_muxid,
					__cobalt_mutex_timedlock, shadow, to);
	} while (err == -EINTR);

	if (!err)
		shadow->lockcnt = 1;
	return -err;
}

int __wrap_pthread_mutex_trylock(pthread_mutex_t *mutex)
{
	union __xeno_mutex *_mutex = (union __xeno_mutex *)mutex;
	struct __shadow_mutex *shadow = &_mutex->shadow_mutex;
	unsigned long status;
	struct timespec ts;
	xnhandle_t cur;
	int err;

	cur = xeno_get_current();
	if (cur == XN_NO_HANDLE)
		return EPERM;

	if (unlikely(shadow->magic != COBALT_MUTEX_MAGIC))
		return EINVAL;

	status = xeno_get_current_mode();
	if (likely((status & (XNRELAX|XNOTHER)) == 0)) {
		err = xnsynch_fast_acquire(get_ownerp(shadow), cur);
		if (likely(err == 0)) {
			shadow->lockcnt = 1;
			return 0;
		}
	} else {
		err = xnsynch_fast_owner_check(get_ownerp(shadow), cur);
		if (err < 0)
			goto do_syscall;

		err = -EBUSY;
	}

	if (err == -EBUSY && shadow->attr.type == PTHREAD_MUTEX_RECURSIVE) {
		if (shadow->lockcnt == UINT_MAX)
			return EAGAIN;

		++shadow->lockcnt;
		return 0;
	}

	return EBUSY;

do_syscall:

	__RT(clock_gettime(CLOCK_REALTIME, &ts));
	do {
		err = XENOMAI_SKINCALL2(__cobalt_muxid,
					__cobalt_mutex_timedlock, shadow, &ts);
	} while (err == -EINTR);

	if (err) {
		if (err == -ETIMEDOUT || err == -EDEADLK)
			return EBUSY;
		return -err;
	}

	shadow->lockcnt = 1;

	return 0;
}

int __wrap_pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	union __xeno_mutex *_mutex = (union __xeno_mutex *)mutex;
	struct __shadow_mutex *shadow = &_mutex->shadow_mutex;
	xnarch_atomic_t *ownerp;
	xnhandle_t cur, owner;
	int err;

	if (unlikely(shadow->magic != COBALT_MUTEX_MAGIC))
		return EINVAL;

	cur = xeno_get_current();
	if (cur == XN_NO_HANDLE)
		return EPERM;

	ownerp = get_ownerp(shadow);
	owner = xnarch_atomic_get(ownerp);
	if (xnhandle_mask_spares(owner) != cur)
		return EPERM;

	if (shadow->lockcnt > 1) {
		--shadow->lockcnt;
		return 0;
	}

	if ((owner & COBALT_MUTEX_COND_SIGNAL))
		goto do_syscall;

	if (unlikely(xeno_get_current_mode() & XNOTHER))
		goto do_syscall;

	if (likely(xnsynch_fast_release(ownerp, cur)))
		return 0;

do_syscall:

	do {
		err = XENOMAI_SKINCALL1(__cobalt_muxid,
					__cobalt_mutex_unlock, shadow);
	} while (err == -EINTR);

	return -err;
}

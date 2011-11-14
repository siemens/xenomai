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
#include <kernel/cobalt/cb_lock.h>
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

	if (unlikely(cb_try_read_lock(&shadow->lock, s)))
		goto checked;

	err = -XENOMAI_SKINCALL2(__cobalt_muxid,__cobalt_check_init,shadow,attr);

	if (err) {
		cb_read_unlock(&shadow->lock, s);
		return err;
	}

  checked:
	cb_force_write_lock(&shadow->lock, s);

	err = -XENOMAI_SKINCALL2(__cobalt_muxid,__cobalt_mutex_init,shadow,attr);

	if (!shadow->attr.pshared)
		shadow->owner = (xnarch_atomic_t *)
			(xeno_sem_heap[0] + shadow->owner_offset);

	cb_write_unlock(&shadow->lock, s);

	return err;
}

int __wrap_pthread_mutex_destroy(pthread_mutex_t *mutex)
{
	union __xeno_mutex *_mutex = (union __xeno_mutex *)mutex;
	struct __shadow_mutex *shadow = &_mutex->shadow_mutex;
	int err;

	if (unlikely(cb_try_write_lock(&shadow->lock, s)))
		return EINVAL;

	err = -XENOMAI_SKINCALL1(__cobalt_muxid, __cobalt_mutex_destroy, shadow);

	cb_write_unlock(&shadow->lock, s);

	return err;
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

	status = xeno_get_current_mode();

	if (unlikely(cb_try_read_lock(&shadow->lock, s)))
		return EINVAL;

	if (shadow->magic != COBALT_MUTEX_MAGIC) {
		err = -EINVAL;
		goto out;
	}

	/*
	 * We track resource ownership for non real-time shadows in
	 * order to handle the auto-relax feature, so we must always
	 * obtain them via a syscall.
	 */
	if (likely(!(status & (XNRELAX|XNOTHER)))) {
		err = xnsynch_fast_acquire(get_ownerp(shadow), cur);

		if (likely(!err)) {
			shadow->lockcnt = 1;
			cb_read_unlock(&shadow->lock, s);
			return 0;
		}

		if (err == -EBUSY)
			switch(shadow->attr.type) {
			case PTHREAD_MUTEX_NORMAL:
				break;

			case PTHREAD_MUTEX_ERRORCHECK:
				err = -EDEADLK;
				goto out;

			case PTHREAD_MUTEX_RECURSIVE:
				if (shadow->lockcnt == UINT_MAX) {
					err = -EAGAIN;
					goto out;
				}
				++shadow->lockcnt;
				err = 0;
				goto out;
			}
		}

	do {
		err = XENOMAI_SKINCALL1(__cobalt_muxid,__cobalt_mutex_lock,shadow);
	} while (err == -EINTR);

  out:
	cb_read_unlock(&shadow->lock, s);

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

	status = xeno_get_current_mode();

	if (unlikely(cb_try_read_lock(&shadow->lock, s)))
		return EINVAL;

	if (shadow->magic != COBALT_MUTEX_MAGIC) {
		err = -EINVAL;
		goto out;
	}

	/* See __wrap_pthread_mutex_lock() */
	if (likely(!(status & (XNRELAX|XNOTHER)))) {
		err = xnsynch_fast_acquire(get_ownerp(shadow), cur);

		if (likely(!err)) {
			shadow->lockcnt = 1;
			cb_read_unlock(&shadow->lock, s);
			return 0;
		}

		if (err == -EBUSY)
			switch(shadow->attr.type) {
			case PTHREAD_MUTEX_NORMAL:
				break;

			case PTHREAD_MUTEX_ERRORCHECK:
				err = -EDEADLK;
				goto out;

			case PTHREAD_MUTEX_RECURSIVE:
				if (shadow->lockcnt == UINT_MAX) {
					err = -EAGAIN;
					goto out;
				}

				++shadow->lockcnt;
				goto out;
			}
	}

	do {
		err = XENOMAI_SKINCALL2(__cobalt_muxid,
					__cobalt_mutex_timedlock, shadow, to);
	} while (err == -EINTR);

  out:
	cb_read_unlock(&shadow->lock, s);

	return -err;
}

int __wrap_pthread_mutex_trylock(pthread_mutex_t *mutex)
{
	union __xeno_mutex *_mutex = (union __xeno_mutex *)mutex;
	struct __shadow_mutex *shadow = &_mutex->shadow_mutex;
	xnarch_atomic_t *ownerp;
	struct timespec ts;
	xnhandle_t cur;
	int err;

	cur = xeno_get_current();
	if (cur == XN_NO_HANDLE)
		return EPERM;

	if (unlikely(cb_try_read_lock(&shadow->lock, s)))
		return EINVAL;

	if (unlikely(shadow->magic != COBALT_MUTEX_MAGIC)) {
		err = EINVAL;
		goto out;
	}

	ownerp = get_ownerp(shadow);
	err = xnsynch_fast_owner_check(ownerp, cur);
	if (err == 0) {
		if (shadow->attr.type == PTHREAD_MUTEX_RECURSIVE) {
			if (shadow->lockcnt == UINT_MAX)
				err = EAGAIN;
			else {
				++shadow->lockcnt;
				err = 0;
			}
		} else
			err = EBUSY;
		goto out;
	}

	if (unlikely(xeno_get_current_mode() & (XNOTHER | XNRELAX)))
		goto do_syscall;

	err = xnsynch_fast_acquire(ownerp, cur);

	if (likely(!err)) {
		shadow->lockcnt = 1;
		cb_read_unlock(&shadow->lock, s);
		return 0;
	}

	err = EBUSY;
	goto out;

do_syscall:

	__RT(clock_gettime(CLOCK_REALTIME, &ts));
	do {
		err = -XENOMAI_SKINCALL2(__cobalt_muxid,
					 __cobalt_mutex_timedlock, shadow, &ts);
	} while (err == EINTR);
	if (err == ETIMEDOUT || err == EDEADLK)
		err = EBUSY;

out:
	cb_read_unlock(&shadow->lock, s);

	return err;
}

int __wrap_pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	union __xeno_mutex *_mutex = (union __xeno_mutex *)mutex;
	struct __shadow_mutex *shadow = &_mutex->shadow_mutex;
	xnarch_atomic_t *ownerp;
	xnhandle_t cur;
	int err;

	cur = xeno_get_current();
	if (cur == XN_NO_HANDLE)
		return EPERM;

	if (unlikely(cb_try_read_lock(&shadow->lock, s)))
		return EINVAL;

	if (unlikely(shadow->magic != COBALT_MUTEX_MAGIC)) {
		err = -EINVAL;
		goto out_err;
	}

	ownerp = get_ownerp(shadow);

	err = xnsynch_fast_owner_check(ownerp, cur);
	if (unlikely(err))
		goto out_err;

	if (shadow->lockcnt > 1) {
		--shadow->lockcnt;
		goto out;
	}

	if (unlikely(xeno_get_current_mode() & XNOTHER))
		goto do_syscall;

	if (unlikely(xnsynch_fast_check_spares(ownerp, COBALT_MUTEX_COND_SIGNAL)))
		goto do_syscall;

	if (likely(xnsynch_fast_release(ownerp, cur))) {
	  out:
		cb_read_unlock(&shadow->lock, s);
		return 0;
	}

do_syscall:

	do {
		err = XENOMAI_SKINCALL1(__cobalt_muxid,
					__cobalt_mutex_unlock, shadow);
	} while (err == -EINTR);

  out_err:
	cb_read_unlock(&shadow->lock, s);

	return -err;
}

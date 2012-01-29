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
#include <posix/mutex.h>
#include <posix/syscall.h>
#include <posix/cb_lock.h>
#include <asm-generic/bits/current.h>

extern int __pse51_muxid;

#ifdef CONFIG_XENO_FASTSYNCH
#define PSE51_MUTEX_MAGIC (0x86860303)

extern unsigned long xeno_sem_heap[2];

static xnarch_atomic_t *get_ownerp(struct __shadow_mutex *shadow)
{
	if (likely(!shadow->attr.pshared))
		return shadow->owner;

	return (xnarch_atomic_t *) (xeno_sem_heap[1] + shadow->owner_offset);
}
#endif /* CONFIG_XENO_FASTSYNCH */

int __wrap_pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
	return -XENOMAI_SKINCALL1(__pse51_muxid, __pse51_mutexattr_init, attr);
}

int __wrap_pthread_mutexattr_destroy(pthread_mutexattr_t *attr)
{
	return -XENOMAI_SKINCALL1(__pse51_muxid,__pse51_mutexattr_destroy,attr);
}

int __wrap_pthread_mutexattr_gettype(const pthread_mutexattr_t *attr,
				     int *type)
{
	return -XENOMAI_SKINCALL2(__pse51_muxid,
				  __pse51_mutexattr_gettype, attr, type);
}

int __wrap_pthread_mutexattr_settype(pthread_mutexattr_t *attr,
				     int type)
{
	return -XENOMAI_SKINCALL2(__pse51_muxid,
				  __pse51_mutexattr_settype, attr, type);
}

int __wrap_pthread_mutexattr_getprotocol(const pthread_mutexattr_t *attr,
					 int *proto)
{
	return -XENOMAI_SKINCALL2(__pse51_muxid,
				  __pse51_mutexattr_getprotocol, attr, proto);
}

int __wrap_pthread_mutexattr_setprotocol(pthread_mutexattr_t *attr,
					 int proto)
{
	return -XENOMAI_SKINCALL2(__pse51_muxid,
				  __pse51_mutexattr_setprotocol, attr, proto);
}

int __wrap_pthread_mutexattr_getpshared(const pthread_mutexattr_t *attr,
					int *pshared)
{
	return -XENOMAI_SKINCALL2(__pse51_muxid,
				  __pse51_mutexattr_getpshared, attr, pshared);
}

int __wrap_pthread_mutexattr_setpshared(pthread_mutexattr_t *attr, int pshared)
{
	return -XENOMAI_SKINCALL2(__pse51_muxid,
				  __pse51_mutexattr_setpshared, attr, pshared);
}

int __wrap_pthread_mutex_init(pthread_mutex_t *mutex,
			      const pthread_mutexattr_t *attr)
{
	union __xeno_mutex *_mutex = (union __xeno_mutex *)mutex;
	struct __shadow_mutex *shadow = &_mutex->shadow_mutex;
	int err;

#ifdef CONFIG_XENO_FASTSYNCH
	if (unlikely(cb_try_read_lock(&shadow->lock, s)))
		goto checked;

	err = -XENOMAI_SKINCALL2(__pse51_muxid,__pse51_check_init,shadow,attr);

	if (err) {
		cb_read_unlock(&shadow->lock, s);
		return err;
	}

  checked:
	cb_force_write_lock(&shadow->lock, s);
#endif /* CONFIG_XENO_FASTSYNCH */

	err = -XENOMAI_SKINCALL2(__pse51_muxid,__pse51_mutex_init,shadow,attr);

#ifdef CONFIG_XENO_FASTSYNCH
	if (!shadow->attr.pshared)
		shadow->owner = (xnarch_atomic_t *)
			(xeno_sem_heap[0] + shadow->owner_offset);

	cb_write_unlock(&shadow->lock, s);
#endif /* CONFIG_XENO_FASTSYNCH */

	return err;
}

int __wrap_pthread_mutex_destroy(pthread_mutex_t *mutex)
{
	union __xeno_mutex *_mutex = (union __xeno_mutex *)mutex;
	struct __shadow_mutex *shadow = &_mutex->shadow_mutex;
	int err;

	if (unlikely(cb_try_write_lock(&shadow->lock, s)))
		return EINVAL;

	err = -XENOMAI_SKINCALL1(__pse51_muxid, __pse51_mutex_destroy, shadow);

	cb_write_unlock(&shadow->lock, s);

	return err;
}

int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex)
{
	union __xeno_mutex *_mutex = (union __xeno_mutex *)mutex;
	struct __shadow_mutex *shadow = &_mutex->shadow_mutex;
	int err;

#ifdef CONFIG_XENO_FASTSYNCH
	unsigned long status;
	xnhandle_t cur;

	cur = xeno_get_current();
	if (unlikely(cur == XN_NO_HANDLE))
		return EPERM;

	if (unlikely(cb_try_read_lock(&shadow->lock, s)))
		return EINVAL;

	if (unlikely(shadow->magic != PSE51_MUTEX_MAGIC)) {
		err = -EINVAL;
		goto out;
	}

	/*
	 * We track resource ownership for non real-time shadows in
	 * order to handle the auto-relax feature, so we must always
	 * obtain them via a syscall.
	 */
	status = xeno_get_current_mode();
	if (unlikely(status & (XNRELAX|XNOTHER)))
		goto do_syscall;

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

  do_syscall:
#endif /* CONFIG_XENO_FASTSYNCH */

	do {
		err = XENOMAI_SKINCALL1(__pse51_muxid,__pse51_mutex_lock,shadow);
	} while (err == -EINTR);

#ifdef CONFIG_XENO_FASTSYNCH
  out:
	cb_read_unlock(&shadow->lock, s);
#endif /* CONFIG_XENO_FASTSYNCH */

	return -err;
}

int __wrap_pthread_mutex_timedlock(pthread_mutex_t *mutex,
				   const struct timespec *to)
{
	union __xeno_mutex *_mutex = (union __xeno_mutex *)mutex;
	struct __shadow_mutex *shadow = &_mutex->shadow_mutex;
	int err;

#ifdef CONFIG_XENO_FASTSYNCH
	unsigned long status;
	xnhandle_t cur;

	cur = xeno_get_current();
	if (unlikely(cur == XN_NO_HANDLE))
		return EPERM;

	if (unlikely(cb_try_read_lock(&shadow->lock, s)))
		return EINVAL;

	if (unlikely(shadow->magic != PSE51_MUTEX_MAGIC)) {
		err = -EINVAL;
		goto out;
	}

	/* See __wrap_pthread_mutex_lock() */
	status = xeno_get_current_mode();
	if (unlikely(status & (XNRELAX|XNOTHER)))
		goto do_syscall;

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

  do_syscall:
#endif /* CONFIG_XENO_FASTSYNCH */

	do {
		err = XENOMAI_SKINCALL2(__pse51_muxid,
					__pse51_mutex_timedlock, shadow, to);
	} while (err == -EINTR);

#ifdef CONFIG_XENO_FASTSYNCH
  out:
	cb_read_unlock(&shadow->lock, s);
#endif /* CONFIG_XENO_FASTSYNCH */

	return -err;
}

int __wrap_pthread_mutex_trylock(pthread_mutex_t *mutex)
{
	union __xeno_mutex *_mutex = (union __xeno_mutex *)mutex;
	struct __shadow_mutex *shadow = &_mutex->shadow_mutex;
	int err;

#ifdef CONFIG_XENO_FASTSYNCH
	extern int __wrap_clock_gettime(clockid_t clock_id, struct timespec *tp);
	struct timespec ts;
	unsigned long status;
	xnhandle_t cur;

	cur = xeno_get_current();
	if (unlikely(cur == XN_NO_HANDLE))
		return EPERM;

	if (unlikely(cb_try_read_lock(&shadow->lock, s)))
		return EINVAL;

	if (unlikely(shadow->magic != PSE51_MUTEX_MAGIC)) {
		err = -EINVAL;
		goto out;
	}

	status = xeno_get_current_mode();
	if (unlikely(status & XNOTHER))
		goto do_syscall;

	if (unlikely(status & XNRELAX)) {
		do {
			err = XENOMAI_SYSCALL1(__xn_sys_migrate,
					       XENOMAI_XENO_DOMAIN);
		} while (err == -EINTR);

		if (err < 0)
			goto out;
	}

	err = xnsynch_fast_acquire(get_ownerp(shadow), cur);

	if (likely(!err)) {
		shadow->lockcnt = 1;
		cb_read_unlock(&shadow->lock, s);
		return 0;
	}

	if (err == -EBUSY && shadow->attr.type == PTHREAD_MUTEX_RECURSIVE) {
		if (shadow->lockcnt == UINT_MAX)
			err = -EAGAIN;
		else {
			++shadow->lockcnt;
			err = 0;
		}
	} else
		err = -EBUSY;

  out:
	cb_read_unlock(&shadow->lock, s);
	return -err;

do_syscall:
	__wrap_clock_gettime(CLOCK_REALTIME, &ts);
	do {
		err = XENOMAI_SKINCALL2(__pse51_muxid,
					__pse51_mutex_timedlock, shadow, &ts);
	} while (err == -EINTR);
	if (err == -ETIMEDOUT || err == -EDEADLK)
		err = -EBUSY;

	cb_read_unlock(&shadow->lock, s);

#else /* !CONFIG_XENO_FASTSYNCH */

	do {
		err = XENOMAI_SKINCALL1(__pse51_muxid,
					__pse51_mutex_trylock, shadow);
	} while (err == -EINTR);

#endif /* !CONFIG_XENO_FASTSYNCH */

	return -err;
}

int __wrap_pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	union __xeno_mutex *_mutex = (union __xeno_mutex *)mutex;
	struct __shadow_mutex *shadow = &_mutex->shadow_mutex;
	int err;

#ifdef CONFIG_XENO_FASTSYNCH
	xnarch_atomic_t *ownerp;
	unsigned long status;
	xnhandle_t cur;

	cur = xeno_get_current();
	if (cur == XN_NO_HANDLE)
		return EPERM;

	if (unlikely(cb_try_read_lock(&shadow->lock, s)))
		return EINVAL;

	if (unlikely(shadow->magic != PSE51_MUTEX_MAGIC)) {
		err = -EINVAL;
		goto out_err;
	}

	status = xeno_get_current_mode();
	if (unlikely(status & XNOTHER))
		goto do_syscall;

	ownerp = get_ownerp(shadow);

	err = xnsynch_fast_owner_check(ownerp, cur);
	if (unlikely(err))
		goto out_err;

	if (shadow->lockcnt > 1) {
		--shadow->lockcnt;
		goto out;
	}

	if (likely(xnsynch_fast_release(ownerp, cur))) {
	  out:
		cb_read_unlock(&shadow->lock, s);
		return 0;
	}

do_syscall:
#endif /* CONFIG_XENO_FASTSYNCH */

	do {
		err = XENOMAI_SKINCALL1(__pse51_muxid,
					__pse51_mutex_unlock, shadow);
	} while (err == -EINTR);

#ifdef CONFIG_XENO_FASTSYNCH
  out_err:
	cb_read_unlock(&shadow->lock, s);
#endif /* CONFIG_XENO_FASTSYNCH */

	return -err;
}

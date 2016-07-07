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
#include <assert.h>
#include <unistd.h>

#include <nucleus/synch.h>
#include <posix/mutex.h>
#include <posix/syscall.h>
#include <posix/cb_lock.h>
#include <asm-generic/current.h>
#include <asm-generic/sem_heap.h>

extern int __pse51_muxid;
static union __xeno_mutex autoinit_mutex_union;
static pthread_mutex_t *const autoinit_mutex =
	&autoinit_mutex_union.native_mutex;

#define PSE51_MUTEX_MAGIC (0x86860303)

#ifdef CONFIG_XENO_FASTSYNCH
static xnarch_atomic_t *get_ownerp(struct __shadow_mutex *shadow)
{
	if (likely(!shadow->attr.pshared))
		return shadow->owner;

	return (xnarch_atomic_t *) (xeno_sem_heap[1] + shadow->owner_offset);
}
#endif /* CONFIG_XENO_FASTSYNCH */

static int mutex_autoinit(pthread_mutex_t *mutex);

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

	if (unlikely(shadow->magic != PSE51_MUTEX_MAGIC))
		goto autoinit;

  start:
	if (unlikely(cb_try_read_lock(&shadow->lock, s)))
		return EINVAL;

	if (unlikely(shadow->magic != PSE51_MUTEX_MAGIC)) {
		err = -EINVAL;
		goto out;
	}

	/*
	 * We track resource ownership for non real-time shadows in
	 * order to handle the auto-relax feature, so we must always
	 * obtain them via a syscall. This is not necessary if the mutex
	 * is recursive and we already own it though.
	 */
	status = xeno_get_current_mode();
	if ((status & (XNRELAX|XNOTHER)) == 0) {
		err = xnsynch_fast_acquire(get_ownerp(shadow), cur);
		if (likely(!err)) {
			shadow->lockcnt = 1;
			goto out;
		}
	} else {
		err = xnsynch_fast_owner_check(get_ownerp(shadow), cur);
		if (err == 0)
			err = -EBUSY;
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
#else /* !CONFIG_XENO_FASTSYNCH */
	if (unlikely(shadow->magic != PSE51_MUTEX_MAGIC))
		goto autoinit;

  start:
#endif /* !CONFIG_XENO_FASTSYNCH */

	do {
		err = XENOMAI_SKINCALL1(__pse51_muxid,__pse51_mutex_lock,shadow);
	} while (err == -EINTR);

#ifdef CONFIG_XENO_FASTSYNCH
  out:
	cb_read_unlock(&shadow->lock, s);
#endif /* CONFIG_XENO_FASTSYNCH */

	return -err;

  autoinit:
	err = mutex_autoinit(mutex);
	if (err)
		return err;
	goto start;
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

	if (unlikely(shadow->magic != PSE51_MUTEX_MAGIC))
		goto autoinit;

  start:
	if (unlikely(cb_try_read_lock(&shadow->lock, s)))
		return EINVAL;

	if (unlikely(shadow->magic != PSE51_MUTEX_MAGIC)) {
		err = -EINVAL;
		goto out;
	}

	/* See __wrap_pthread_mutex_lock() */
	status = xeno_get_current_mode();
	if ((status & (XNRELAX|XNOTHER)) == 0) {
		err = xnsynch_fast_acquire(get_ownerp(shadow), cur);
		if (likely(!err)) {
			shadow->lockcnt = 1;
			goto out;
		}
	} else {
		err = xnsynch_fast_owner_check(get_ownerp(shadow), cur);
		if (err == 0)
			err = -EBUSY;
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

#else /* !CONFIG_XENO_FASTSYNCH */
	if (unlikely(shadow->magic != PSE51_MUTEX_MAGIC))
		goto autoinit;

  start:
#endif /* !CONFIG_XENO_FASTSYNCH */

	do {
		err = XENOMAI_SKINCALL2(__pse51_muxid,
					__pse51_mutex_timedlock, shadow, to);
	} while (err == -EINTR);

#ifdef CONFIG_XENO_FASTSYNCH
  out:
	cb_read_unlock(&shadow->lock, s);
#endif /* CONFIG_XENO_FASTSYNCH */

	return -err;

  autoinit:
	err = mutex_autoinit(mutex);
	if (err)
		return err;
	goto start;
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

	if (unlikely(shadow->magic != PSE51_MUTEX_MAGIC))
		goto autoinit;

  start:
	if (unlikely(cb_try_read_lock(&shadow->lock, s)))
		return EINVAL;

	if (unlikely(shadow->magic != PSE51_MUTEX_MAGIC)) {
		err = -EINVAL;
		goto out;
	}

	status = xeno_get_current_mode();
	if ((status & (XNRELAX|XNOTHER)) == 0) {
		err = xnsynch_fast_acquire(get_ownerp(shadow), cur);
		if (likely(!err)) {
			shadow->lockcnt = 1;
			goto out;
		}
	} else {
		err = xnsynch_fast_owner_check(get_ownerp(shadow), cur);
		if (err < 0)
			goto do_syscall;

		err = -EBUSY;
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
	if (unlikely(shadow->magic != PSE51_MUTEX_MAGIC))
		goto autoinit;

  start:
	do {
		err = XENOMAI_SKINCALL1(__pse51_muxid,
					__pse51_mutex_trylock, shadow);
	} while (err == -EINTR);

#endif /* !CONFIG_XENO_FASTSYNCH */

	return -err;

  autoinit:
	err = mutex_autoinit(mutex);
	if (err)
		return err;
	goto start;
}

int __wrap_pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	union __xeno_mutex *_mutex = (union __xeno_mutex *)mutex;
	struct __shadow_mutex *shadow = &_mutex->shadow_mutex;
	int err;

#ifdef CONFIG_XENO_FASTSYNCH
	xnarch_atomic_t *ownerp;
	xnhandle_t cur;

	cur = xeno_get_current();
	if (cur == XN_NO_HANDLE)
		return EPERM;

	if (unlikely(cb_try_read_lock(&shadow->lock, s)))
		return EINVAL;

	if (unlikely(shadow->magic != PSE51_MUTEX_MAGIC)) {
		err = -EINVAL;
		goto out;
	}

	ownerp = get_ownerp(shadow);

	err = xnsynch_fast_owner_check(ownerp, cur);
	if (unlikely(err))
		goto out;

	if (shadow->lockcnt > 1) {
		--shadow->lockcnt;
		goto out;
	}

	if (unlikely(xeno_get_current_mode() & XNOTHER))
		goto do_syscall;

	if (likely(xnsynch_fast_release(ownerp, cur)))
		goto out;

do_syscall:
#endif /* CONFIG_XENO_FASTSYNCH */

	do {
		err = XENOMAI_SKINCALL1(__pse51_muxid,
					__pse51_mutex_unlock, shadow);
	} while (err == -EINTR);

#ifdef CONFIG_XENO_FASTSYNCH
  out:
	cb_read_unlock(&shadow->lock, s);
#endif /* CONFIG_XENO_FASTSYNCH */

	return -err;
}

void pse51_mutex_init(void)
{
	struct __shadow_mutex *_mutex = &autoinit_mutex_union.shadow_mutex;
	pthread_mutexattr_t rt_init_mattr;
	int err __attribute__((unused));

	__wrap_pthread_mutexattr_init(&rt_init_mattr);
	__wrap_pthread_mutexattr_setprotocol(&rt_init_mattr, PTHREAD_PRIO_INHERIT);
	_mutex->magic = ~PSE51_MUTEX_MAGIC;
	err = __wrap_pthread_mutex_init(autoinit_mutex, &rt_init_mattr);
	assert(err == 0);
	__wrap_pthread_mutexattr_destroy(&rt_init_mattr);
}

static int __attribute__((cold)) mutex_autoinit(pthread_mutex_t *mutex)
{
	struct __shadow_mutex *_mutex =
		&((union __xeno_mutex *)mutex)->shadow_mutex;
	static pthread_mutex_t uninit_normal_mutex = PTHREAD_MUTEX_INITIALIZER;
	static pthread_mutex_t uninit_recursive_mutex =
		PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
	static pthread_mutex_t uninit_errorcheck_mutex =
		PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;
	int err __attribute__((unused));
	pthread_mutexattr_t mattr;
	int ret = 0, type;

	if (memcmp(mutex, &uninit_normal_mutex, sizeof(*mutex)) == 0)
		type = PTHREAD_MUTEX_DEFAULT;
	else if (memcmp(mutex, &uninit_recursive_mutex, sizeof(*mutex)) == 0)
		type = PTHREAD_MUTEX_RECURSIVE_NP;
	else if (memcmp(mutex, &uninit_errorcheck_mutex, sizeof(*mutex)) == 0)
		type = PTHREAD_MUTEX_ERRORCHECK_NP;
	else
		return EINVAL;

	__wrap_pthread_mutexattr_init(&mattr);
	__wrap_pthread_mutexattr_settype(&mattr, type);
	err = __wrap_pthread_mutex_lock(autoinit_mutex);
	if (err) {
		ret = err;
		goto out;
	}
	if (_mutex->magic != PSE51_MUTEX_MAGIC)
		ret = __wrap_pthread_mutex_init(mutex, &mattr);
	err = __wrap_pthread_mutex_unlock(autoinit_mutex);
	if (err) {
		if (ret == 0)
			ret = err;
	}

  out:
	__wrap_pthread_mutexattr_destroy(&mattr);

	return ret;
}

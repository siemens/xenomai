/*
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
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

#include <limits.h>
#include <nucleus/synch.h>
#include <nucleus/thread.h>
#include <native/syscall.h>
#include <native/mutex.h>
#include <asm-generic/bits/current.h>

extern int __native_muxid;
extern unsigned long xeno_sem_heap[2];

int rt_mutex_create(RT_MUTEX *mutex, const char *name)
{
	int err;

	err = XENOMAI_SKINCALL2(__native_muxid,
				__native_mutex_create, mutex, name);

#ifdef CONFIG_XENO_FASTSYNCH
	if (!err) {
		mutex->fastlock = (xnarch_atomic_t *)
			(xeno_sem_heap[(name && *name) ? 1 : 0] +
			 (unsigned long)mutex->fastlock);
		mutex->lockcnt = 0;
	}
#endif /* CONFIG_XENO_FASTSYNCH */

	return err;
}

int rt_mutex_bind(RT_MUTEX *mutex, const char *name, RTIME timeout)
{
	int err;

	err = XENOMAI_SKINCALL3(__native_muxid,
				__native_mutex_bind, mutex, name, &timeout);

#ifdef CONFIG_XENO_FASTSYNCH
	if (!err) {
		mutex->fastlock = (xnarch_atomic_t *)
			(xeno_sem_heap[(name && *name) ? 1 : 0] +
			 (unsigned long)mutex->fastlock);
		mutex->lockcnt = 0;
	}
#endif /* CONFIG_XENO_FASTSYNCH */

	return err;
}

int rt_mutex_delete(RT_MUTEX *mutex)
{
	return XENOMAI_SKINCALL1(__native_muxid, __native_mutex_delete, mutex);
}

static int rt_mutex_acquire_inner(RT_MUTEX *mutex, RTIME timeout, xntmode_t mode)
{
	int err;
#ifdef CONFIG_XENO_FASTSYNCH
	unsigned long status;
	xnhandle_t cur;

	cur = xeno_get_current();
	if (cur == XN_NO_HANDLE)
		return -EPERM;

	/*
	 * We track resource ownership for non real-time shadows in
	 * order to handle the auto-relax feature, so we must always
	 * obtain them via a syscall.
	 */
	status = xeno_get_current_mode();
	if (unlikely(status & XNOTHER))
		goto do_syscall;

	if (likely(!(status & XNRELAX))) {
		err = xnsynch_fast_acquire(mutex->fastlock, cur);
		if (likely(!err)) {
			mutex->lockcnt = 1;
			return 0;
		}

		if (err == -EBUSY) {
			if (mutex->lockcnt == UINT_MAX)
				return -EAGAIN;

			mutex->lockcnt++;
			return 0;
		}

		if (timeout == TM_NONBLOCK && mode == XN_RELATIVE)
			return -EWOULDBLOCK;
	} else if (xnsynch_fast_owner_check(mutex->fastlock, cur) == 0) {
		/*
		 * The application is buggy as it jumped to secondary mode
		 * while holding the mutex. Nevertheless, we have to keep the
		 * mutex state consistent.
		 *
		 * We make no efforts to migrate or warn here. There is
		 * XENO_DEBUG(SYNCH_RELAX) to catch such bugs.
		 */
		if (mutex->lockcnt == UINT_MAX)
			return -EAGAIN;

		mutex->lockcnt++;
		return 0;
	}
do_syscall:
#endif /* CONFIG_XENO_FASTSYNCH */

	err = XENOMAI_SKINCALL3(__native_muxid,
				__native_mutex_acquire, mutex, mode, &timeout);

#ifdef CONFIG_XENO_FASTSYNCH
	if (!err)
		mutex->lockcnt = 1;
#endif /* CONFIG_XENO_FASTSYNCH */

	return err;
}

int rt_mutex_acquire(RT_MUTEX *mutex, RTIME timeout)
{
	return rt_mutex_acquire_inner(mutex, timeout, XN_RELATIVE);
}

int rt_mutex_acquire_until(RT_MUTEX *mutex, RTIME timeout)
{
	return rt_mutex_acquire_inner(mutex, timeout, XN_REALTIME);
}

int rt_mutex_release(RT_MUTEX *mutex)
{
#ifdef CONFIG_XENO_FASTSYNCH
	unsigned long status;
	xnhandle_t cur;

	cur = xeno_get_current();
	if (cur == XN_NO_HANDLE)
		return -EPERM;

	status = xeno_get_current_mode();
	if (unlikely(status & XNOTHER))
		/* See rt_mutex_acquire_inner() */
		goto do_syscall;

	if (unlikely(xnsynch_fast_owner_check(mutex->fastlock, cur) != 0))
		return -EPERM;

	if (mutex->lockcnt > 1) {
		mutex->lockcnt--;
		return 0;
	}

	if (likely(xnsynch_fast_release(mutex->fastlock, cur)))
		return 0;

do_syscall:
#endif /* CONFIG_XENO_FASTSYNCH */

	return XENOMAI_SKINCALL1(__native_muxid, __native_mutex_release, mutex);
}

int rt_mutex_inquire(RT_MUTEX *mutex, RT_MUTEX_INFO *info)
{
	return XENOMAI_SKINCALL2(__native_muxid,
				 __native_mutex_inquire, mutex, info);
}

/* Compatibility wrappers for pre-2.3 builds. */

int rt_mutex_lock(RT_MUTEX *mutex, RTIME timeout)
{
    return rt_mutex_acquire(mutex, timeout);
}

int rt_mutex_unlock(RT_MUTEX *mutex)
{
    return rt_mutex_release(mutex);
}

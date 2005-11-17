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

#include <features.h>
#include <stddef.h>
#include <errno.h>
#include <xenomai/posix/syscall.h>
#include "posix/pthread.h"

extern int __pse51_muxid;

int __wrap_pthread_mutex_init (pthread_mutex_t *mutex,
			       const pthread_mutexattr_t *attr)
{
    union __xeno_mutex *_mutex = (union __xeno_mutex *)mutex;
    int err;

    /* Mutex attributes are ignored, since we always build recursive,
       PIP-enabled mutexes. */

    err = -XENOMAI_SKINCALL1(__pse51_muxid,
			     __pse51_mutex_init,
			     &_mutex->shadow_mutex.handle);
    if (!err)
	_mutex->shadow_mutex.magic = SHADOW_MUTEX_MAGIC;

    return err;
}

int __wrap_pthread_mutex_destroy (pthread_mutex_t *mutex)

{
    union __xeno_mutex *_mutex = (union __xeno_mutex *)mutex;

    if (_mutex->shadow_mutex.magic != SHADOW_MUTEX_MAGIC)
	return EINVAL;

    return -XENOMAI_SKINCALL1(__pse51_muxid,
			      __pse51_mutex_destroy,
			      _mutex->shadow_mutex.handle);
}

int __wrap_pthread_mutex_lock (pthread_mutex_t *mutex)

{
    union __xeno_mutex *_mutex = (union __xeno_mutex *)mutex;
    int err;

    do {
        if (_mutex->shadow_mutex.magic != SHADOW_MUTEX_MAGIC)
            return EINVAL;

        err = XENOMAI_SKINCALL1(__pse51_muxid,
                                __pse51_mutex_lock,
                                _mutex->shadow_mutex.handle);
    } while (err == -EIDRM || err == -EINTR);

    return -err;
}

int __wrap_pthread_mutex_timedlock (pthread_mutex_t *mutex,
				    const struct timespec *to)
{
    union __xeno_mutex *_mutex = (union __xeno_mutex *)mutex;
    int err;

    do {
        if (_mutex->shadow_mutex.magic != SHADOW_MUTEX_MAGIC)
            return EINVAL;

        err = XENOMAI_SKINCALL2(__pse51_muxid,
                                __pse51_mutex_timedlock,
                                _mutex->shadow_mutex.handle,
                                to);
    } while (err == -EIDRM || err == -EINTR);

    return -err;
}

int __wrap_pthread_mutex_trylock (pthread_mutex_t *mutex)

{
    union __xeno_mutex *_mutex = (union __xeno_mutex *)mutex;

    if (_mutex->shadow_mutex.magic != SHADOW_MUTEX_MAGIC)
	return EINVAL;

    return -XENOMAI_SKINCALL1(__pse51_muxid,
                              __pse51_mutex_trylock,
                              _mutex->shadow_mutex.handle);
}

int __wrap_pthread_mutex_unlock (pthread_mutex_t *mutex)

{
    union __xeno_mutex *_mutex = (union __xeno_mutex *)mutex;

    if (_mutex->shadow_mutex.magic != SHADOW_MUTEX_MAGIC)
	return EINVAL;

    return -XENOMAI_SKINCALL1(__pse51_muxid,
			      __pse51_mutex_unlock,
			      _mutex->shadow_mutex.handle);
}

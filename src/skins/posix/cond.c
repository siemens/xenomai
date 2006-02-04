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

int __wrap_pthread_cond_init (pthread_cond_t *cond,
			      const pthread_condattr_t *attr)
{
    union __xeno_cond *_cond = (union __xeno_cond *)cond;
    int err;

    err = -XENOMAI_SKINCALL1(__pse51_muxid,
			     __pse51_cond_init,
			     &_cond->shadow_cond);
    return err;
}

int __wrap_pthread_cond_destroy (pthread_cond_t *cond)

{
    union __xeno_cond *_cond = (union __xeno_cond *)cond;

    return -XENOMAI_SKINCALL1(__pse51_muxid,
			      __pse51_cond_destroy,
			      &_cond->shadow_cond);
}

int __wrap_pthread_cond_wait (pthread_cond_t *cond,
			      pthread_mutex_t *mutex)
{
    union __xeno_mutex *_mutex = (union __xeno_mutex *)mutex;
    union __xeno_cond *_cond = (union __xeno_cond *)cond;
    int err, oldtype;

    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

    err = XENOMAI_SKINCALL2(__pse51_muxid,
                            __pse51_cond_wait,
                            &_cond->shadow_cond,
                            &_mutex->shadow_mutex);

    pthread_setcanceltype(oldtype, NULL);

    return err == -EINTR ? 0 : -err;
}

int __wrap_pthread_cond_timedwait (pthread_cond_t *cond,
				   pthread_mutex_t *mutex,
				   const struct timespec *abstime)
{
    union __xeno_mutex *_mutex = (union __xeno_mutex *)mutex;
    union __xeno_cond *_cond = (union __xeno_cond *)cond;
    int err, oldtype;

    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

    err = XENOMAI_SKINCALL3(__pse51_muxid,
                            __pse51_cond_timedwait,
                            &_cond->shadow_cond,
                            &_mutex->shadow_mutex,
                            abstime);

    pthread_setcanceltype(oldtype, NULL);

    return err == -EINTR ? 0 : -err;
}

int __wrap_pthread_cond_signal (pthread_cond_t *cond)

{
    union __xeno_cond *_cond = (union __xeno_cond *)cond;

    return -XENOMAI_SKINCALL1(__pse51_muxid,
			      __pse51_cond_signal,
			      &_cond->shadow_cond);
}

int __wrap_pthread_cond_broadcast (pthread_cond_t *cond)

{
    union __xeno_cond *_cond = (union __xeno_cond *)cond;

    return -XENOMAI_SKINCALL1(__pse51_muxid,
			      __pse51_cond_broadcast,
			      &_cond->shadow_cond);
}

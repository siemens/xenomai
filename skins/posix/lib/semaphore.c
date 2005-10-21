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

#include <fcntl.h>              /* For O_CREAT. */
#include <stdarg.h>
#include <errno.h>
#include <posix/syscall.h>
#include <posix/lib/semaphore.h>

extern int __pse51_muxid;

int __wrap_sem_init (sem_t *sem,
		     int pshared,
		     unsigned value)
{
    union __xeno_semaphore *_sem = (union __xeno_semaphore *)sem;
    int err;

    err = -XENOMAI_SKINCALL3(__pse51_muxid,
			     __pse51_sem_init,
			     &_sem->handle,
			     pshared,
			     value);
    if (!err)
	return 0;

    errno = err;

    return -1;
}

int __wrap_sem_destroy (sem_t *sem)

{
    int err;

    err = -XENOMAI_SKINCALL1(__pse51_muxid,
			     __pse51_sem_destroy,
			     sem);
    if (!err)
	return 0;

    errno = err;

    return -1;
}

int __wrap_sem_post (sem_t *sem)

{
    int err;

    err = -XENOMAI_SKINCALL1(__pse51_muxid,
			     __pse51_sem_post,
			     sem);
    if (!err)
	return 0;

    errno = err;

    return -1;
}

int __wrap_sem_wait (sem_t *sem)

{
    int err;

    err = -XENOMAI_SKINCALL1(__pse51_muxid,
			     __pse51_sem_wait,
			     sem);
    if (!err)
	return 0;

    errno = err;

    return -1;
}

int __wrap_sem_timedwait (sem_t *sem, const struct timespec *ts)

{
    int err;

    err = -XENOMAI_SKINCALL2(__pse51_muxid,
			     __pse51_sem_timedwait,
			     sem,
                             ts);
    if (!err)
	return 0;

    errno = err;

    return -1;
}

int __wrap_sem_trywait (sem_t *sem)

{
    int err;

    err = -XENOMAI_SKINCALL1(__pse51_muxid,
			     __pse51_sem_trywait,
			     sem);
    if (!err)
	return 0;

    errno = err;

    return -1;
}

int __wrap_sem_getvalue (sem_t *sem, int *sval)

{
    int err;

    err = -XENOMAI_SKINCALL2(__pse51_muxid,
			     __pse51_sem_getvalue,
			     sem,
			     sval);
    if (!err)
	return 0;

    errno = err;

    return -1;
}

sem_t *__wrap_sem_open (const char *name, int oflags, ...)
{
    unsigned long handle;
    unsigned value = 0;
    mode_t mode = 0;
    va_list ap;
    int err;

    if((oflags & O_CREAT))
        {
        va_start(ap, oflags);
        mode = va_arg(ap, int);
        value = va_arg(ap, unsigned);
        va_end(ap);
        }

    err = -XENOMAI_SKINCALL5(__pse51_muxid,
                             __pse51_sem_open,
                             &handle,
                             name,
                             oflags,
                             mode,
                             value);
    if (!err)
        return (sem_t *) handle;

    errno = err;

    return SEM_FAILED;
}

int __wrap_sem_close (sem_t *sem)
{
    int err;

    err = -XENOMAI_SKINCALL1(__pse51_muxid,
                             __pse51_sem_close,
                             sem);

    if (!err)
        return 0;

    errno = err;

    return -1;
}

int __wrap_sem_unlink (const char *name)
{
    int err;

    err = -XENOMAI_SKINCALL1(__pse51_muxid, __pse51_sem_unlink, name);

    if (!err)
        return 0;

    errno = err;

    return -1;
}

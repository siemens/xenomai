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

#ifndef _XENO_POSIX_SEMAPHORE_H
#define _XENO_POSIX_SEMAPHORE_H

#include <fcntl.h>              /* For sem_open flags. */
#include_next <semaphore.h>

union __xeno_semaphore {
    sem_t native_sem;
    unsigned long handle;
};

#ifdef __cplusplus
extern "C" {
#endif

int __real_sem_init(sem_t *sem,
		    int pshared,
		    unsigned value);

int __real_sem_destroy(sem_t *sem);

int __real_sem_post(sem_t *sem);

int __real_sem_wait(sem_t *sem);

sem_t *__real_sem_open(const char *name, int oflags, ...);

int __real_sem_close(sem_t *sem);

int __real_sem_unlink(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* _XENO_POSIX_SEMAPHORE_H */

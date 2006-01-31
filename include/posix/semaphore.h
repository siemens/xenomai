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

#if defined(__KERNEL__) || defined(__XENO_SIM__)

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/fcntl.h>
#endif /* __KERNEL__ */

#ifdef __XENO_SIM__
#include <posix_overrides.h>
#endif /* __XENO_SIM__ */

#define SEM_VALUE_MAX (INT_MAX)
#define SEM_FAILED    NULL

typedef struct pse51_sem {
    unsigned magic;
    xnholder_t link;            /* Link in pse51_semq */
    xnsynch_t synchbase;
    int value;
} sem_t;

#ifdef __cplusplus
extern "C" {
#endif

#undef sem_init
#define sem_init(s,p,v) pse51_sem_init((s),(p),(v))

int pse51_sem_init(sem_t *sem,
                   int pshared,
                   unsigned int value);

int sem_destroy(sem_t *sem);

int sem_post(sem_t *sem);

int sem_trywait(sem_t *sem);

int sem_wait(sem_t *sem);

int sem_timedwait(sem_t *sem,
		  const struct timespec *abs_timeout);

int sem_getvalue(sem_t *sem,
		 int *value);

sem_t *sem_open(const char *name, int oflag, ...);

int sem_close(sem_t *sem);

int sem_unlink(const char *name);

#ifdef __cplusplus
}
#endif

#else /* !(__KERNEL__ || __XENO_SIM__) */

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

#endif /* !(__KERNEL__ || __XENO_SIM__) */

#endif /* _XENO_POSIX_SEMAPHORE_H */

/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _COBALT_SEMAPHORE_H
#define _COBALT_SEMAPHORE_H

#ifdef __KERNEL__

#include <linux/kernel.h>
#include <linux/fcntl.h>

#define SEM_VALUE_MAX (INT_MAX)
#define SEM_FAILED    NULL

/* Copied from Linuxthreads semaphore.h. */
struct _sem_fastlock
{
  long int __status;
  int __spinlock;
};

typedef struct
{
  struct _sem_fastlock __sem_lock;
  int __sem_value;
  long __sem_waiting;
} sem_t;

#else /* !__KERNEL__ */

#include <fcntl.h>              /* For sem_open flags. */
#include_next <semaphore.h>
#include <cobalt/wrappers.h>

#ifdef __cplusplus
extern "C" {
#endif

COBALT_DECL(int, sem_init(sem_t *sem,
			  int pshared,
			  unsigned int value));

COBALT_DECL(int, sem_destroy(sem_t *sem));

COBALT_DECL(int, sem_post(sem_t *sem));

COBALT_DECL(int, sem_wait(sem_t *sem));

COBALT_DECL(int, sem_timedwait(sem_t *sem,
			       const struct timespec *abs_timeout));

COBALT_DECL(int, sem_trywait(sem_t *sem));

COBALT_DECL(int, sem_getvalue(sem_t *sem, int *value));

COBALT_DECL(sem_t *, sem_open(const char *name, int oflags, ...));

COBALT_DECL(int, sem_close(sem_t *sem));

COBALT_DECL(int, sem_unlink(const char *name));

int sem_init_np(sem_t *sem,
		int flags,
		unsigned int value);

int sem_broadcast_np(sem_t *sem);

#ifdef __cplusplus
}
#endif

#endif /* !__KERNEL__ */

struct cobalt_sem;

union cobalt_sem_union {
	sem_t native_sem;
	struct __shadow_sem {
		unsigned int magic;
		struct cobalt_sem *sem;
	} shadow_sem;
};

/* For Cobalt's sem_init_np() extension. */
#define SEM_FIFO     0x1
#define SEM_PULSE    0x2
#define SEM_PSHARED  0x4
#define SEM_REPORT   0x8
#define SEM_WARNDEL  0x10
#define SEM_RAWCLOCK 0x20

#endif /* !_COBALT_SEMAPHORE_H */

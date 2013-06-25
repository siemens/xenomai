/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
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
#ifndef _COBALT_POSIX_SEM_H
#define _COBALT_POSIX_SEM_H

#include <linux/kernel.h>
#include <linux/fcntl.h>
#include <cobalt/kernel/thread.h>
#include <cobalt/kernel/registry.h>

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

#include <cobalt/uapi/sem.h>

#define SEM_VALUE_MAX (INT_MAX)
#define SEM_FAILED    NULL

typedef struct {
    u_long uaddr;
    unsigned refcnt;
    cobalt_assoc_t assoc;

#define assoc2usem(laddr) container_of(laddr, cobalt_usem_t, assoc)
} cobalt_usem_t;

void cobalt_sem_usems_cleanup(struct cobalt_context *cc);

struct cobalt_sem;

int sem_getvalue(struct cobalt_sem *sem, int *value);

int sem_post_inner(struct cobalt_sem *sem,
		   struct cobalt_kqueues *ownq, int bcast);

int cobalt_sem_init(struct __shadow_sem __user *u_sem,
		    int pshared, unsigned value);

int cobalt_sem_post(struct __shadow_sem __user *u_sem);

int cobalt_sem_wait(struct __shadow_sem __user *u_sem);

int cobalt_sem_timedwait(struct __shadow_sem __user *u_sem,
			 struct timespec __user *u_ts);

int cobalt_sem_trywait(struct __shadow_sem __user *u_sem);

int cobalt_sem_getvalue(struct __shadow_sem __user *u_sem,
			int __user *u_sval);

int cobalt_sem_destroy(struct __shadow_sem __user *u_sem);

int cobalt_sem_open(unsigned long __user *u_addr,
		    const char __user *u_name,
		    int oflags, mode_t mode, unsigned value);

int cobalt_sem_close(unsigned long uaddr, int __user *u_closed);

int cobalt_sem_unlink(const char __user *u_name);

int cobalt_sem_init_np(struct __shadow_sem __user *u_sem,
		       int flags, unsigned value);

int cobalt_sem_broadcast_np(struct __shadow_sem __user *u_sem);

void cobalt_semq_cleanup(struct cobalt_kqueues *q);

void cobalt_sem_pkg_init(void);

void cobalt_sem_pkg_cleanup(void);

#endif /* !_COBALT_POSIX_SEM_H */

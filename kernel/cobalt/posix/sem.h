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
#include <xenomai/posix/syscall.h>

struct cobalt_process;

struct cobalt_sem {
	unsigned int magic;
	struct xnsynch synchbase;
	/** semq */
	struct list_head link;
	struct cobalt_sem_state *state;
	int flags;
	struct cobalt_kqueues *owningq;
	xnhandle_t handle;
	unsigned int refs;
};

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

#define SEM_VALUE_MAX	(INT_MAX)
#define SEM_FAILED	NULL
#define SEM_NAMED	0x80000000

struct cobalt_sem_shadow __user *
__cobalt_sem_open(struct cobalt_sem_shadow __user *usm,
		  const char __user *u_name,
		  int oflags, mode_t mode, unsigned int value);

int __cobalt_sem_timedwait(struct cobalt_sem_shadow __user *u_sem,
			   const void __user *u_ts,
			   int (*fetch_timeout)(struct timespec *ts,
						const void __user *u_ts));

int __cobalt_sem_destroy(xnhandle_t handle);

void __cobalt_sem_unlink(xnhandle_t handle);

void cobalt_sem_usems_cleanup(struct cobalt_process *cc);

struct cobalt_sem *
__cobalt_sem_init(const char *name, struct cobalt_sem_shadow *sem,
		  int flags, unsigned value);

COBALT_SYSCALL_DECL(sem_init,
		    int, (struct cobalt_sem_shadow __user *u_sem,
			  int flags, unsigned value));

COBALT_SYSCALL_DECL(sem_post,
		    int, (struct cobalt_sem_shadow __user *u_sem));

COBALT_SYSCALL_DECL(sem_wait,
		    int, (struct cobalt_sem_shadow __user *u_sem));

COBALT_SYSCALL_DECL(sem_timedwait,
		    int, (struct cobalt_sem_shadow __user *u_sem,
			  struct timespec __user *u_ts));

COBALT_SYSCALL_DECL(sem_trywait,
		    int, (struct cobalt_sem_shadow __user *u_sem));

COBALT_SYSCALL_DECL(sem_getvalue, int,
		    (struct cobalt_sem_shadow __user *u_sem,
		     int __user *u_sval));

COBALT_SYSCALL_DECL(sem_destroy,
		    int, (struct cobalt_sem_shadow __user *u_sem));

COBALT_SYSCALL_DECL(sem_open,
		    int, (struct cobalt_sem_shadow __user *__user *u_addrp,
			  const char __user *u_name,
			  int oflags, mode_t mode, unsigned int value));

COBALT_SYSCALL_DECL(sem_close,
		    int, (struct cobalt_sem_shadow __user *usm));

COBALT_SYSCALL_DECL(sem_unlink, int, (const char __user *u_name));

COBALT_SYSCALL_DECL(sem_broadcast_np,
		    int, (struct cobalt_sem_shadow __user *u_sem));

COBALT_SYSCALL_DECL(sem_inquire,
		    int, (struct cobalt_sem_shadow __user *u_sem,
			  struct cobalt_sem_info __user *u_info,
			  pid_t __user *u_waitlist,
			  size_t waitsz));

void cobalt_semq_cleanup(struct cobalt_kqueues *q);

void cobalt_sem_pkg_init(void);

void cobalt_sem_pkg_cleanup(void);

#endif /* !_COBALT_POSIX_SEM_H */

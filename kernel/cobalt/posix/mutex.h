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

#ifndef _COBALT_POSIX_MUTEX_H
#define _COBALT_POSIX_MUTEX_H

#include "thread.h"
#include <cobalt/uapi/mutex.h>

typedef struct cobalt_mutexattr pthread_mutexattr_t;

struct cobalt_mutex {
	unsigned int magic;
	xnsynch_t synchbase;
	/** cobalt_mutexq */
	struct list_head link;
	struct list_head conds;
	pthread_mutexattr_t attr;
	struct cobalt_kqueues *owningq;
};

extern const pthread_mutexattr_t cobalt_default_mutex_attr;

int cobalt_mutexattr_init(pthread_mutexattr_t __user *u_attr);

int cobalt_mutexattr_destroy(pthread_mutexattr_t __user *u_attr);

int cobalt_mutexattr_gettype(const pthread_mutexattr_t __user *u_attr,
			     int __user *u_type);

int cobalt_mutexattr_settype(pthread_mutexattr_t __user *u_attr,
			     int type);

int cobalt_mutexattr_getprotocol(const pthread_mutexattr_t __user *u_attr,
				 int __user *u_proto);

int cobalt_mutexattr_setprotocol(pthread_mutexattr_t __user *u_attr,
				 int proto);

int cobalt_mutexattr_getpshared(const pthread_mutexattr_t __user *u_attr,
				int __user *u_pshared);

int cobalt_mutexattr_setpshared(pthread_mutexattr_t __user *u_attr,
				int pshared);

int cobalt_mutex_check_init(struct __shadow_mutex __user *u_mx);

int cobalt_mutex_init(struct __shadow_mutex __user *u_mx,
		      const pthread_mutexattr_t __user *u_attr);

int cobalt_mutex_destroy(struct __shadow_mutex __user *u_mx);

int cobalt_mutex_trylock(struct __shadow_mutex __user *u_mx);

int cobalt_mutex_lock(struct __shadow_mutex __user *u_mx);

int cobalt_mutex_timedlock(struct __shadow_mutex __user *u_mx,
			   const struct timespec __user *u_ts);

int cobalt_mutex_unlock(struct __shadow_mutex __user *u_mx);

int cobalt_mutex_acquire_unchecked(struct xnthread *cur,
				   struct cobalt_mutex *mutex,
				   int timed,
				   const struct timespec __user *u_ts);

int cobalt_mutex_release(struct xnthread *cur,
			 struct cobalt_mutex *mutex);

void cobalt_mutexq_cleanup(struct cobalt_kqueues *q);

void cobalt_mutex_pkg_init(void);

void cobalt_mutex_pkg_cleanup(void);

#endif /* !_COBALT_POSIX_MUTEX_H */

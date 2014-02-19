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
#ifndef _COBALT_POSIX_COND_H
#define _COBALT_POSIX_COND_H

#include <linux/time.h>
#include <linux/list.h>
#include <cobalt/kernel/synch.h>
#include <cobalt/uapi/thread.h>
#include <cobalt/uapi/cond.h>

struct cobalt_kqueues;
struct cobalt_mutex;

typedef struct cobalt_condattr pthread_condattr_t;

struct cobalt_cond {
	unsigned int magic;
	struct xnsynch synchbase;
	/** cobalt_condq */
	struct list_head link;
	struct list_head mutex_link;
	unsigned long *pending_signals;
	pthread_condattr_t attr;
	struct cobalt_mutex *mutex;
	struct cobalt_kqueues *owningq;
	xnhandle_t handle;
};

extern const pthread_condattr_t cobalt_default_cond_attr;

int cobalt_cond_deferred_signals(struct cobalt_cond *cond);

int cobalt_condattr_init(pthread_condattr_t __user *u_attr);

int cobalt_condattr_destroy(pthread_condattr_t __user *u_attr);

int cobalt_condattr_getclock(const pthread_condattr_t __user *u_attr,
			     clockid_t __user *u_clock);

int cobalt_condattr_setclock(pthread_condattr_t __user *u_attr, clockid_t clock);

int cobalt_condattr_getpshared(const pthread_condattr_t __user *u_attr,
			       int __user *u_pshared);

int cobalt_condattr_setpshared(pthread_condattr_t __user *u_attr, int pshared);

int cobalt_cond_init(struct cobalt_cond_shadow __user *u_cnd,
		     const pthread_condattr_t __user *u_attr);

int cobalt_cond_destroy(struct cobalt_cond_shadow __user *u_cnd);

int cobalt_cond_wait_prologue(struct cobalt_cond_shadow __user *u_cnd,
			      struct cobalt_mutex_shadow __user *u_mx,
			      int *u_err,
			      unsigned int timed,
			      struct timespec __user *u_ts);

int cobalt_cond_wait_epilogue(struct cobalt_cond_shadow __user *u_cnd,
			      struct cobalt_mutex_shadow __user *u_mx);

void cobalt_condq_cleanup(struct cobalt_kqueues *q);

void cobalt_cond_pkg_init(void);

void cobalt_cond_pkg_cleanup(void);

#endif /* !_COBALT_POSIX_COND_H */

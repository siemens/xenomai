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

#ifndef _COBALT_COND_H
#define _COBALT_COND_H

#include <pthread.h>

struct cobalt_cond;
struct mutex_dat;

union cobalt_cond_union {
	pthread_cond_t native_cond;
	struct __shadow_cond {
		unsigned magic;
		struct cobalt_condattr attr;
		struct cobalt_cond *cond;
		union {
			unsigned pending_signals_offset;
			unsigned long *pending_signals;
		};
		union {
			unsigned mutex_datp_offset;
			struct mutex_dat *mutex_datp;
		};
	} shadow_cond;
};

#define COBALT_COND_MAGIC 0x86860505

#ifdef __KERNEL__

#include "internal.h"

struct __shadow_mutex;
union cobalt_mutex_union;

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
};

extern const pthread_condattr_t cobalt_default_cond_attr;

static inline int cobalt_cond_deferred_signals(struct cobalt_cond *cond)
{
	unsigned long pending_signals;
	int need_resched;

	pending_signals = *cond->pending_signals;

	switch(pending_signals) {
	default:
		*cond->pending_signals = 0;
		need_resched = xnsynch_wakeup_many_sleepers(&cond->synchbase,
							    pending_signals);
		break;

	case ~0UL:
		need_resched =
			xnsynch_flush(&cond->synchbase, 0) == XNSYNCH_RESCHED;
		*cond->pending_signals = 0;
		break;

	case 0:
		need_resched = 0;
		break;
	}

	return need_resched;
}

int cobalt_condattr_init(pthread_condattr_t __user *u_attr);

int cobalt_condattr_destroy(pthread_condattr_t __user *u_attr);

int cobalt_condattr_getclock(const pthread_condattr_t __user *u_attr,
			     clockid_t __user *u_clock);

int cobalt_condattr_setclock(pthread_condattr_t __user *u_attr, clockid_t clock);

int cobalt_condattr_getpshared(const pthread_condattr_t __user *u_attr,
			       int __user *u_pshared);

int cobalt_condattr_setpshared(pthread_condattr_t __user *u_attr, int pshared);

int cobalt_cond_init(struct __shadow_cond __user *u_cnd,
		     const pthread_condattr_t __user *u_attr);

int cobalt_cond_destroy(struct __shadow_cond __user *u_cnd);

int cobalt_cond_wait_prologue(struct __shadow_cond __user *u_cnd,
			      struct __shadow_mutex __user *u_mx,
			      int *u_err,
			      unsigned int timed,
			      struct timespec __user *u_ts);

int cobalt_cond_wait_epilogue(struct __shadow_cond __user *u_cnd,
			      struct __shadow_mutex __user *u_mx);

void cobalt_condq_cleanup(struct cobalt_kqueues *q);

void cobalt_cond_pkg_init(void);

void cobalt_cond_pkg_cleanup(void);

#endif /* __KERNEL__ */

#endif /* !_COBALT_COND_H */

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

#ifndef _POSIX_MUTEX_H
#define _POSIX_MUTEX_H

#include <pthread.h>
#include <asm/xenomai/atomic.h>

struct cobalt_mutex;

union __xeno_mutex {
	pthread_mutex_t native_mutex;
	struct __shadow_mutex {
		unsigned magic;
		unsigned lockcnt;
		struct cobalt_mutex *mutex;
		union {
			unsigned owner_offset;
			xnarch_atomic_t *owner;
		};
		struct cobalt_mutexattr attr;

#define COBALT_MUTEX_COND_SIGNAL XN_HANDLE_SPARE2
	} shadow_mutex;
};

#if defined(__KERNEL__) || defined(__XENO_SIM__)

#include "internal.h"
#include "thread.h"
#include "cond.h"

typedef struct cobalt_mutex {
	unsigned magic;
	xnsynch_t synchbase;
	xnholder_t link;            /* Link in cobalt_mutexq */

#define link2mutex(laddr)                                               \
	((cobalt_mutex_t *)(((char *)laddr) - offsetof(cobalt_mutex_t, link)))

	xnqueue_t conds;

	pthread_mutexattr_t attr;
	cobalt_kqueues_t *owningq;
} cobalt_mutex_t;

extern pthread_mutexattr_t cobalt_default_mutex_attr;

/* must be called with nklock locked, interrupts off. */
static inline int cobalt_mutex_acquire_unchecked(xnthread_t *cur,
						 cobalt_mutex_t *mutex,
						 int timed,
						 xnticks_t abs_to)

{
	if (xnsynch_owner_check(&mutex->synchbase, cur) == 0)
		return -EBUSY;

	if (timed)
		xnsynch_acquire(&mutex->synchbase, abs_to, XN_REALTIME);
	else
		xnsynch_acquire(&mutex->synchbase, XN_INFINITE, XN_RELATIVE);

	if (unlikely(xnthread_test_info(cur, XNBREAK | XNRMID | XNTIMEO))) {
		if (xnthread_test_info(cur, XNBREAK))
			return -EINTR;
		else if (xnthread_test_info(cur, XNTIMEO))
			return -ETIMEDOUT;
		else /* XNRMID */
			return -EINVAL;
	}

	return 0;
}

static inline int cobalt_mutex_release(xnthread_t *cur, cobalt_mutex_t *mutex)
{
	xnholder_t *holder;
	int need_resched;

	if (!cobalt_obj_active(mutex, COBALT_MUTEX_MAGIC, struct cobalt_mutex))
		 return -EINVAL;

#if XENO_DEBUG(POSIX)
	if (mutex->owningq != cobalt_kqueues(mutex->attr.pshared))
		return -EPERM;
#endif /* XENO_DEBUG(POSIX) */

	if (xnsynch_owner_check(&mutex->synchbase, cur) != 0)
		return -EPERM;

	need_resched = 0;
	for (holder = getheadq(&mutex->conds);
	     holder; holder = nextq(&mutex->conds, holder)) {
		struct cobalt_cond *cond = mutex_link2cond(holder);
		if (*(cond->pending_signals)) {
			if (xnsynch_nsleepers(&cond->synchbase))
				need_resched |=
					cobalt_cond_deferred_signals(cond);
			else
				*(cond->pending_signals) = 0;
		}
	}
	xnsynch_fast_clear_spares(mutex->synchbase.fastlock,
				  xnthread_handle(cur),
				  COBALT_MUTEX_COND_SIGNAL);
	need_resched |= xnsynch_release(&mutex->synchbase) != NULL;

	return need_resched;
	/* Do not reschedule here, releasing the mutex and suspension must be
	   done atomically in pthread_cond_*wait. */
}

int cobalt_mutex_check_init(union __xeno_mutex __user *u_mx);

int cobalt_mutex_init(union __xeno_mutex __user *u_mx,
		      const pthread_mutexattr_t __user *u_attr);

int cobalt_mutex_destroy(union __xeno_mutex __user *u_mx);

int cobalt_mutex_trylock(union __xeno_mutex __user *u_mx);

int cobalt_mutex_lock(union __xeno_mutex __user *u_mx);

int cobalt_mutex_timedlock(union __xeno_mutex __user *u_mx,
			   const struct timespec __user *u_ts);

int cobalt_mutex_unlock(union __xeno_mutex __user *u_mx);

void cobalt_mutexq_cleanup(cobalt_kqueues_t *q);

void cobalt_mutex_pkg_init(void);

void cobalt_mutex_pkg_cleanup(void);
#endif /* __KERNEL__ */

#endif /* !_POSIX_MUTEX_H */

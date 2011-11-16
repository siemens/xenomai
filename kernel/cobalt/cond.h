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


#ifndef _POSIX_COND_H
#define _POSIX_COND_H

#include <pthread.h>

struct cobalt_cond;
struct mutex_dat;

union __xeno_cond {
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

#if defined(__KERNEL__) || defined(__XENO_SIM__)

#include "internal.h"

struct __shadow_mutex;
union __xeno_mutex;

typedef struct cobalt_cond {
	unsigned magic;
	xnsynch_t synchbase;
	xnholder_t link;	/* Link in cobalt_condq */

#define link2cond(laddr)                                                \
    ((cobalt_cond_t *)(((char *)laddr) - offsetof(cobalt_cond_t, link)))

	xnholder_t mutex_link;

#define mutex_link2cond(laddr)						\
    ((cobalt_cond_t *)(((char *)laddr) - offsetof(cobalt_cond_t, mutex_link)))

	unsigned long *pending_signals;
	pthread_condattr_t attr;
	struct cobalt_mutex *mutex;
	cobalt_kqueues_t *owningq;
} cobalt_cond_t;

static inline int cobalt_cond_deferred_signals(struct cobalt_cond *cond)
{
	unsigned long pending_signals;
	int need_resched, i;

	pending_signals = *(cond->pending_signals);

	switch(pending_signals) {
	case 0:
		need_resched = 0;
		break;

	default:
		for(i = 0, need_resched = 0; i < pending_signals; i++) {
			if (xnsynch_wakeup_one_sleeper(&cond->synchbase) == NULL)
				break;
			need_resched = 1;
		}
		*cond->pending_signals = 0;
		break;

	case ~0UL:
		need_resched =
			xnsynch_flush(&cond->synchbase, 0) == XNSYNCH_RESCHED;
		*cond->pending_signals = 0;
	}

	return need_resched;
}

int cobalt_cond_init(union __xeno_cond __user *u_cnd,
		     const pthread_condattr_t __user *u_attr);

int cobalt_cond_destroy(union __xeno_cond __user *u_cnd);

int cobalt_cond_wait_prologue(union __xeno_cond __user *u_cnd,
			      union __xeno_mutex __user *u_mx,
			      int *u_err,
			      unsigned int timed,
			      struct timespec __user *u_ts);

int cobalt_cond_wait_epilogue(union __xeno_cond __user *u_cnd,
			      union __xeno_mutex __user *u_mx);

void cobalt_condq_cleanup(cobalt_kqueues_t *q);

void cobalt_cond_pkg_init(void);

void cobalt_cond_pkg_cleanup(void);

#endif /* __KERNEL__ */

#endif /* !_POSIX_COND_H */

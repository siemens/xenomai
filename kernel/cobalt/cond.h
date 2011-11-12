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

union __xeno_cond {
	pthread_cond_t native_cond;
	struct __shadow_cond {
		unsigned magic;
#ifdef CONFIG_XENO_FASTSYNCH
		struct cobalt_condattr attr;
		union {
			unsigned pending_signals_offset;
			unsigned long *pending_signals;
		};
		union {
			unsigned mutex_ownerp_offset;
			xnarch_atomic_t *mutex_ownerp;
		};
#endif /* CONFIG_XENO_FASTSYNCH */
		struct cobalt_cond *cond;
	} shadow_cond;
};

#if defined(__KERNEL__) || defined(__XENO_SIM__)

#include "internal.h"

struct __shadow_mutex;

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

int cobalt_cond_timedwait_prologue(xnthread_t *cur,
				  struct __shadow_cond *shadow,
				  struct __shadow_mutex *mutex,
				  unsigned *count_ptr,
				  int timed,
				  xnticks_t to);

int cobalt_cond_timedwait_epilogue(xnthread_t *cur,
				  struct __shadow_cond *shadow,
				  struct __shadow_mutex *mutex, unsigned count);

#ifdef CONFIG_XENO_FASTSYNCH
int cobalt_cond_deferred_signals(struct cobalt_cond *cond);
#endif /* CONFIG_XENO_FASTSYNCH */

void cobalt_condq_cleanup(cobalt_kqueues_t *q);

void cobalt_cond_pkg_init(void);

void cobalt_cond_pkg_cleanup(void);

#endif /* __KERNEL__ */

#endif /* !_POSIX_COND_H */

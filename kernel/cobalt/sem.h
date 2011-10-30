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

#ifndef _POSIX_SEM_H
#define _POSIX_SEM_H

#include <nucleus/thread.h>       /* For cobalt_current_thread and
				   cobalt_thread_t definition. */
#include <nucleus/registry.h>     /* For assocq */

#ifndef __XENO_SIM__

typedef struct {
    u_long uaddr;
    unsigned refcnt;
    cobalt_assoc_t assoc;

#define assoc2usem(laddr) \
    ((cobalt_usem_t *)((unsigned long) (laddr) - offsetof(cobalt_usem_t, assoc)))
} cobalt_usem_t;

void cobalt_sem_usems_cleanup(cobalt_queues_t *q);

#endif /* !__XENO_SIM__ */

void cobalt_semq_cleanup(cobalt_kqueues_t *q);

void cobalt_sem_pkg_init(void);

void cobalt_sem_pkg_cleanup(void);

int sem_post_inner(struct cobalt_sem *sem,
		   cobalt_kqueues_t *ownq, int bcast);

#endif /* !_POSIX_SEM_H */

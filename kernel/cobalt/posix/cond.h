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

#include <linux/types.h>
#include <linux/time.h>
#include <linux/list.h>
#include <cobalt/kernel/synch.h>
#include <cobalt/uapi/thread.h>
#include <cobalt/uapi/cond.h>
#include <xenomai/posix/syscall.h>

struct cobalt_kqueues;
struct cobalt_mutex;

struct cobalt_cond {
	unsigned int magic;
	struct xnsynch synchbase;
	/** cobalt_condq */
	struct list_head link;
	struct list_head mutex_link;
	struct cobalt_cond_state *state;
	struct cobalt_condattr attr;
	struct cobalt_mutex *mutex;
	struct cobalt_kqueues *owningq;
	xnhandle_t handle;
};

COBALT_SYSCALL_DECL(cond_init,
		    int, (struct cobalt_cond_shadow __user *u_cnd,
			  const struct cobalt_condattr __user *u_attr));

COBALT_SYSCALL_DECL(cond_destroy,
		    int, (struct cobalt_cond_shadow __user *u_cnd));

COBALT_SYSCALL_DECL(cond_wait_prologue,
		    int, (struct cobalt_cond_shadow __user *u_cnd,
			  struct cobalt_mutex_shadow __user *u_mx,
			  int *u_err,
			  unsigned int timed,
			  struct timespec __user *u_ts));

COBALT_SYSCALL_DECL(cond_wait_epilogue,
		    int, (struct cobalt_cond_shadow __user *u_cnd,
			  struct cobalt_mutex_shadow __user *u_mx));

int cobalt_cond_deferred_signals(struct cobalt_cond *cond);

void cobalt_condq_cleanup(struct cobalt_kqueues *q);

void cobalt_cond_pkg_init(void);

void cobalt_cond_pkg_cleanup(void);

#endif /* !_COBALT_POSIX_COND_H */

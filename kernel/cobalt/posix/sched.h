/*
 * Copyright (C) 2009 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _COBALT_POSIX_SCHED_H
#define _COBALT_POSIX_SCHED_H

#include <linux/list.h>
#include <cobalt/kernel/sched.h>

struct cobalt_kqueues;

struct cobalt_sched_group {
	struct xnsched_quota_group quota;
	struct cobalt_kqueues *kq;
	int pshared;
	struct list_head next;
};

struct xnsched_class *
cobalt_sched_policy_param(union xnsched_policy_param *param,
			  int u_policy, const struct sched_param_ex *param_ex,
			  xnticks_t *tslice_r);

int cobalt_sched_yield(void);

int cobalt_sched_weighted_prio(int policy,
			       const struct sched_param_ex __user *u_param);

int cobalt_sched_min_prio(int policy);

int cobalt_sched_max_prio(int policy);

int cobalt_sched_setconfig_np(int cpu,
			      int policy,
			      const union sched_config __user *u_config,
			      size_t len);

ssize_t cobalt_sched_getconfig_np(int cpu,
				  int policy,
				  union sched_config __user *u_config,
				  size_t len);

void cobalt_sched_cleanup(struct cobalt_kqueues *q);

void cobalt_sched_pkg_init(void);

void cobalt_sched_pkg_cleanup(void);

#endif /* !_COBALT_POSIX_SCHED_H */

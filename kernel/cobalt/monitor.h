/*
 * Copyright (C) 2011 Philippe Gerum <rpm@xenomai.org>
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

#ifndef _COBALT_MONITOR_H
#define _COBALT_MONITOR_H

#include <asm/xenomai/atomic.h>

#define COBALT_MONITOR_GRANTED    0x01
#define COBALT_MONITOR_DRAINED    0x02
#define COBALT_MONITOR_SIGNALED   0x03 /* i.e. GRANTED or DRAINED */
#define COBALT_MONITOR_BROADCAST  0x04
#define COBALT_MONITOR_PENDED     0x08

struct cobalt_monitor_data {
	xnarch_atomic_t owner;
	unsigned long flags;
};

#ifdef __KERNEL__

#include <nucleus/synch.h>

struct cobalt_kqueues;

struct cobalt_monitor {
	unsigned int magic;
	struct xnsynch gate;
	struct xnsynch drain;
	struct cobalt_monitor_data *data;
	struct cobalt_kqueues *owningq;
	struct xnholder link;
	struct xnqueue waiters;
	int flags;
};

int cobalt_monitor_init(struct cobalt_monitor_shadow __user *u_monsh,
			int flags);

int cobalt_monitor_enter(struct cobalt_monitor_shadow __user *u_monsh);

int cobalt_monitor_sync(struct cobalt_monitor_shadow __user *u_monsh);

int cobalt_monitor_exit(struct cobalt_monitor_shadow __user *u_monsh);

int cobalt_monitor_wait(struct cobalt_monitor_shadow __user *u_monsh,
			int event, const struct timespec __user *u_ts,
			int __user *u_ret);

int cobalt_monitor_destroy(struct cobalt_monitor_shadow __user *u_monsh);

void cobalt_monitorq_cleanup(struct cobalt_kqueues *q);

void cobalt_monitor_pkg_init(void);

void cobalt_monitor_pkg_cleanup(void);

#endif /* __KERNEL__ */

#endif /* !_COBALT_MONITOR_H */

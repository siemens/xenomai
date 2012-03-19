/*
 * Copyright (C) 2012 Philippe Gerum <rpm@xenomai.org>
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

#ifndef _POSIX_EVENT_H
#define _POSIX_EVENT_H

#define COBALT_EVENT_PENDED  0x1

struct cobalt_event_data {
	unsigned long value;
	unsigned long flags;
	int nwaiters;
};

#ifdef __KERNEL__

#include <nucleus/synch.h>

struct cobalt_kqueues;

struct cobalt_event {
	unsigned int magic;
	unsigned long value;
	struct xnsynch synch;
	struct cobalt_event_data *data;
	struct cobalt_kqueues *owningq;
	struct xnholder link;
	int flags;
};

int cobalt_event_init(struct cobalt_event_shadow __user *u_evtsh,
		      unsigned long value,
		      int flags);

int cobalt_event_wait(struct cobalt_event_shadow __user *u_evtsh,
		      unsigned long bits,
		      unsigned long __user *u_bits_r,
		      int mode,
		      struct timespec __user *u_ts);

int cobalt_event_sync(struct cobalt_event_shadow __user *u_evtsh);

int cobalt_event_destroy(struct cobalt_event_shadow __user *u_evtsh);

void cobalt_eventq_cleanup(struct cobalt_kqueues *q);

void cobalt_event_pkg_init(void);

void cobalt_event_pkg_cleanup(void);

#endif /* __KERNEL__ */

#endif /* !_POSIX_EVENT_H */

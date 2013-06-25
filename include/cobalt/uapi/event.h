/*
 * Copyright (C) 2013 Philippe Gerum <rpm@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */
#ifndef _COBALT_UAPI_EVENT_H
#define _COBALT_UAPI_EVENT_H

struct cobalt_event_data {
	unsigned long value;
	unsigned long flags;
#define COBALT_EVENT_PENDED  0x1
	int nwaiters;
};

struct cobalt_event;

/* Creation flags. */
#define COBALT_EVENT_FIFO    0x0
#define COBALT_EVENT_PRIO    0x1
#define COBALT_EVENT_SHARED  0x2

/* Wait mode. */
#define COBALT_EVENT_ALL  0x0
#define COBALT_EVENT_ANY  0x1

struct cobalt_event_shadow {
	struct cobalt_event *event;
	union {
		struct cobalt_event_data *data;
		unsigned int data_offset;
	} u;
	int flags;
};

typedef struct cobalt_event_shadow cobalt_event_t;

#endif /* !_COBALT_UAPI_EVENT_H */

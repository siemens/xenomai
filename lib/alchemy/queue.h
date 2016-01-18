/*
 * Copyright (C) 2011 Philippe Gerum <rpm@xenomai.org>.
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

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#ifndef _ALCHEMY_QUEUE_H
#define _ALCHEMY_QUEUE_H

#include <boilerplate/list.h>
#include <copperplate/syncobj.h>
#include <copperplate/registry.h>
#include <copperplate/cluster.h>
#include <copperplate/heapobj.h>
#include <alchemy/queue.h>

struct alchemy_queue {
	unsigned int magic;	/* Must be first. */
	char name[XNOBJECT_NAME_LEN];
	int mode;
	size_t limit;
	struct heapobj hobj;
	struct syncobj sobj;
	struct clusterobj cobj;
	struct listobj mq;
	unsigned int mcount;
	struct fsobj fsobj;
};

#define queue_magic	0x8787ebeb

struct alchemy_queue_msg {
	size_t size;
	unsigned int refcount;
	struct holder next;
	/* Payload data follows. */
};

struct alchemy_queue_wait {
	dref_type(struct alchemy_queue_msg *) msg;
	void *local_buf;
	size_t local_bufsz;
};

extern struct syncluster alchemy_queue_table;

#endif /* _ALCHEMY_QUEUE_H */

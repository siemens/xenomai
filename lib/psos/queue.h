/*
 * Copyright (C) 2008 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _PSOS_QUEUE_H
#define _PSOS_QUEUE_H

#include <sys/types.h>
#include <boilerplate/hash.h>
#include <copperplate/syncobj.h>
#include <copperplate/cluster.h>

#define Q_VARIABLE  0x40000000
#define Q_JAMMED    0x80000000

struct psos_queue {
	unsigned int magic;		/* Must be first. */
	char name[XNOBJECT_NAME_LEN];

	u_long flags;
	u_long maxmsg;
	u_long maxlen;
	u_long msgcount;

	struct syncobj sobj;
	struct listobj msg_list;
	struct clusterobj cobj;
};

struct psos_queue_wait {
	size_t size;
	dref_type(void *) ptr;
};

extern struct cluster psos_queue_table;

#endif /* _PSOS_QUEUE_H */

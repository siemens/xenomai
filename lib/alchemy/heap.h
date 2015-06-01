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

#ifndef _ALCHEMY_HEAP_H
#define _ALCHEMY_HEAP_H

#include <copperplate/syncobj.h>
#include <copperplate/registry.h>
#include <copperplate/cluster.h>
#include <copperplate/heapobj.h>
#include <alchemy/heap.h>

struct alchemy_heap {
	unsigned int magic;	/* Must be first. */
	char name[XNOBJECT_NAME_LEN];
	struct heapobj hobj;
	struct syncobj sobj;
	struct clusterobj cobj;
	int mode;
	size_t size;
	dref_type(void *) sba;
	struct fsobj fsobj;
};

struct alchemy_heap_wait {
	size_t size;
	dref_type (void *) ptr;
};

#define heap_magic	0x8a8aebeb

extern struct syncluster alchemy_heap_table;

#endif /* _ALCHEMY_HEAP_H */

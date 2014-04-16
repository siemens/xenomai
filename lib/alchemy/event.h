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

#ifndef _ALCHEMY_EVENT_H
#define _ALCHEMY_EVENT_H

#include <copperplate/eventobj.h>
#include <copperplate/registry.h>
#include <copperplate/cluster.h>
#include <alchemy/event.h>

struct alchemy_event {
	unsigned int magic;	/* Must be first. */
	char name[XNOBJECT_NAME_LEN];
	struct eventobj evobj;
	struct clusterobj cobj;
	struct fsobj fsobj;
};

#define event_magic	0x8484ebeb

extern struct syncluster alchemy_event_table;

#endif /* _ALCHEMY_EVENT_H */

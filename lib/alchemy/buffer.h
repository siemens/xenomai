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

#ifndef _ALCHEMY_BUFFER_H
#define _ALCHEMY_BUFFER_H

#include <copperplate/registry-obstack.h>
#include <copperplate/syncobj.h>
#include <copperplate/cluster.h>
#include <alchemy/buffer.h>

struct alchemy_buffer {
	unsigned int magic;	/* Must be first. */
	char name[XNOBJECT_NAME_LEN];
	struct syncobj sobj;
	struct clusterobj cobj;
	size_t bufsz;
	int mode;
	dref_type(void *) buf;
	size_t rdoff;
	size_t wroff;
	size_t fillsz;
	struct fsobj fsobj;
};

struct alchemy_buffer_wait {
	size_t size;
};

#define buffer_magic	0x8989ebeb

extern struct syncluster alchemy_buffer_table;

#endif /* _ALCHEMY_BUFFER_H */

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

#ifndef _ALCHEMY_PIPE_H
#define _ALCHEMY_PIPE_H

#include <copperplate/cluster.h>
#include <alchemy/pipe.h>

/* Fixed default for MSG_MORE accumulation. */
#define ALCHEMY_PIPE_STREAMSZ  16384

struct alchemy_pipe {
	unsigned int magic;	/* Must be first. */
	char name[XNOBJECT_NAME_LEN];
	int sock;
	int minor;
	struct clusterobj cobj;
};

#define pipe_magic	0x8b8bebeb

extern struct syncluster alchemy_pipe_table;

#endif /* _ALCHEMY_PIPE_H */

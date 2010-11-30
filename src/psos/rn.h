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

#ifndef _PSOS_RN_H
#define _PSOS_RN_H

#include <copperplate/hash.h>
#include <copperplate/syncobj.h>
#include <copperplate/heapobj.h>
#include <copperplate/cluster.h>

struct psos_rn {
	unsigned int magic;		/* Must be first. */
	char name[32];

	u_long flags;
	u_long length;
	u_long usize;
	u_long busynr;
	u_long usedmem;

	struct syncobj sobj;
	struct heapobj hobj;
	struct pvclusterobj cobj;
};

extern struct pvcluster psos_rn_table;

#endif /* _PSOS_RN_H */

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

#ifndef _ALCHEMY_COND_H
#define _ALCHEMY_COND_H

#include <copperplate/registry.h>
#include <copperplate/cluster.h>
#include <alchemy/cond.h>

struct threadobj;

struct alchemy_cond {
	unsigned int magic;	/* Must be first. */
	char name[XNOBJECT_NAME_LEN];
	pthread_cond_t cond;
	struct clusterobj cobj;
	struct fsobj fsobj;
};

#define cond_magic	0x8686ebeb

extern struct syncluster alchemy_cond_table;

#endif /* _ALCHEMY_COND_H */

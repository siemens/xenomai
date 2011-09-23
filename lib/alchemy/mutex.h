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

#ifndef _ALCHEMY_MUTEX_H
#define _ALCHEMY_MUTEX_H

#include <copperplate/cluster.h>
#include <alchemy/mutex.h>
#include <alchemy/task.h>

struct alchemy_mutex {
	unsigned int magic;	/* Must be first. */
	char name[32];
	pthread_mutex_t lock;
	pthread_mutex_t safe;
	struct clusterobj cobj;
	RT_TASK owner;
	int nwaiters;
};

#define mutex_magic	0x8585ebeb

extern struct cluster alchemy_mutex_table;

struct alchemy_mutex *get_alchemy_mutex(RT_MUTEX *mutex, int *err_r);

void put_alchemy_mutex(struct alchemy_mutex *mcb);

struct alchemy_mutex *find_alchemy_mutex(RT_MUTEX *mutex, int *err_r);

#endif /* _ALCHEMY_MUTEX_H */

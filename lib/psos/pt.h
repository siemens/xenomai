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

#ifndef _PSOS_PT_H
#define _PSOS_PT_H

#include <sys/types.h>
#include <pthread.h>
#include <boilerplate/hash.h>
#include <copperplate/cluster.h>

struct psos_pt {
	unsigned int magic;		/* Must be first. */
	char name[XNOBJECT_NAME_LEN];
	struct pvclusterobj cobj;
	pthread_mutex_t lock;

	unsigned long flags;
	unsigned long bsize;
	unsigned long psize;
	unsigned long nblks;
	unsigned long ublks;

	void *freelist;
	caddr_t data;
	unsigned long bitmap[1];
};

extern struct pvcluster psos_pt_table;

#endif /* _PSOS_PT_H */

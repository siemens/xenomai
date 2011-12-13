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

#ifndef _VXWORKS_SEMLIB_H
#define _VXWORKS_SEMLIB_H

#include <pthread.h>
#include <copperplate/syncobj.h>
#include <vxworks/semLib.h>

struct wind_sem;

struct wind_sem_ops {

	STATUS (*take)(struct wind_sem *, int timeout);
	STATUS (*give)(struct wind_sem *);
	STATUS (*flush)(struct wind_sem *);
	STATUS (*delete)(struct wind_sem *);
};

struct wind_sem {

	unsigned int magic;
	int options;

	union {
		struct {
			struct syncobj sobj;
			int value;
			int maxvalue;
		} xsem;
		struct {
			pthread_mutex_t lock;
			struct threadobj *owner;
			int lockdepth;
		} msem;
	} u;

	const struct wind_sem_ops *semops;
};

#endif /* _VXWORKS_SEMLIB_H */

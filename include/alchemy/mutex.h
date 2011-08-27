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

#ifndef _XENOMAI_ALCHEMY_MUTEX_H
#define _XENOMAI_ALCHEMY_MUTEX_H

#include <stdint.h>
#include <alchemy/timer.h>

struct RT_MUTEX {
	uintptr_t handle;
};

typedef struct RT_MUTEX RT_MUTEX;

struct RT_MUTEX_INFO {
	int locked;
	int nwaiters;
	char name[32];
	char owner[32];
};

typedef struct RT_MUTEX_INFO RT_MUTEX_INFO;

#ifdef __cplusplus
extern "C" {
#endif

int rt_mutex_create(RT_MUTEX *mutex,
		    const char *name);

int rt_mutex_delete(RT_MUTEX *mutex);

int rt_mutex_acquire(RT_MUTEX *mutex,
		     RTIME timeout);

int rt_mutex_acquire_until(RT_MUTEX *mutex,
			   RTIME timeout);

int rt_mutex_release(RT_MUTEX *mutex);

int rt_mutex_inquire(RT_MUTEX *mutex,
		     RT_MUTEX_INFO *info);

#ifdef __cplusplus
}
#endif

#endif /* _XENOMAI_ALCHEMY_MUTEX_H */

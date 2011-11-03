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

#ifndef _XENOMAI_ALCHEMY_HEAP_H
#define _XENOMAI_ALCHEMY_HEAP_H

#include <stdint.h>
#include <alchemy/timer.h>

/*
 * Creation flags.
 */
#define H_PRIO    0x1	/* Pend by task priority order. */
#define H_FIFO    0x0	/* Pend by FIFO order. */
#define H_SINGLE  0x4	/* Manage as single-block area. */
#define H_SHARED  H_SINGLE
/* Deprecated, compat only. */
#define H_MAPPABLE 0x0

struct RT_HEAP {
	uintptr_t handle;
};

typedef struct RT_HEAP RT_HEAP;

struct RT_HEAP_INFO {
	int nwaiters;
	int mode;
	size_t heapsize;
	size_t usablemem;
	size_t usedmem;
	char name[32];
};

typedef struct RT_HEAP_INFO RT_HEAP_INFO;

#ifdef __cplusplus
extern "C" {
#endif

int rt_heap_create(RT_HEAP *heap,
		   const char *name,
		   size_t heapsize,
		   int mode);

int rt_heap_delete(RT_HEAP *heap);

int rt_heap_alloc(RT_HEAP *heap,
		  size_t size,
		  RTIME timeout,
		  void **blockp);

int rt_heap_free(RT_HEAP *heap,
		 void *block);

int rt_heap_inquire(RT_HEAP *heap,
		    RT_HEAP_INFO *info);

int rt_heap_bind(RT_HEAP *heap,
		 const char *name,
		 RTIME timeout);

int rt_heap_unbind(RT_HEAP *heap);

#ifdef __cplusplus
}
#endif

#endif /* _XENOMAI_ALCHEMY_HEAP_H */

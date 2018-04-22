/*
 * Copyright (C) 2018 Philippe Gerum <rpm@xenomai.org>.
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
#include <stdlib.h>
#include "boilerplate/heapmem.h"
#include "copperplate/heapobj.h"
#include "copperplate/debug.h"
#include "copperplate/tunables.h"
#include "xenomai/init.h"

#define MIN_HEAPMEM_HEAPSZ  (64 * 1024)

struct heap_memory heapmem_main;

int __heapobj_init_private(struct heapobj *hobj, const char *name,
			   size_t size, void *mem)
{
	void *_mem = mem;
	int ret;

	if (mem == NULL) {
		_mem = __STD(malloc(size));
		if (_mem == NULL)
			return -ENOMEM;
	}
	
	if (name)
		snprintf(hobj->name, sizeof(hobj->name), "%s", name);
	else
		snprintf(hobj->name, sizeof(hobj->name), "%p", hobj);

	ret = heapmem_init(hobj->pool, _mem, size);
	if (ret) {
		if (mem == NULL)
			__STD(free(_mem));
		return ret;
	}

	hobj->pool = _mem;
	hobj->size = size;

	return 0;
}

int heapobj_init_array_private(struct heapobj *hobj, const char *name,
			       size_t size, int elems)
{
	return __bt(__heapobj_init_private(hobj, name,
			   HEAPMEM_ARENA_SIZE(size * elems), NULL));
}

int heapobj_pkg_init_private(void)
{
	size_t size;
	void *mem;
	int ret;

#ifdef CONFIG_XENO_PSHARED
	size = MIN_HEAPMEM_HEAPSZ;
#else
	size = __copperplate_setup_data.mem_pool;
	if (size < MIN_HEAPMEM_HEAPSZ)
		size = MIN_HEAPMEM_HEAPSZ;
#endif
	size = HEAPMEM_ARENA_SIZE(size);
	mem = __STD(malloc(size));
	if (mem == NULL)
		return -ENOMEM;

	ret = heapmem_init(&heapmem_main, mem, size);
	if (ret) {
		__STD(free(mem));
		return ret;
	}

	return 0;
}

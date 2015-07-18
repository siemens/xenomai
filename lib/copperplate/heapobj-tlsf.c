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

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include "boilerplate/tlsf/tlsf.h"
#include "copperplate/heapobj.h"
#include "copperplate/debug.h"
#include "copperplate/threadobj.h"
#include "xenomai/init.h"
#include "internal.h"

#if __WORDSIZE == 32
#define TLSF_BLOCK_ALIGN  (8 * 2)
#else
#define TLSF_BLOCK_ALIGN  (16 * 2)
#endif

static int tlsf_pool_overhead;

int __heapobj_init_private(struct heapobj *hobj, const char *name,
			   size_t size, void *mem)
{
	if (mem == NULL) {
		/*
		 * When the memory area is unspecified, obtain it from
		 * the main pool, accounting for the TLSF overhead.
		 */
		size += tlsf_pool_overhead;
		mem = tlsf_malloc(size);
		if (mem == NULL)
			return __bt(-ENOMEM);
	}

	if (name)
		snprintf(hobj->name, sizeof(hobj->name), "%s", name);
	else
		snprintf(hobj->name, sizeof(hobj->name), "%p", hobj);

	hobj->pool = mem;
	/* Make sure to wipe out tlsf's signature. */
	memset(mem, 0, size < 32 ? size : 32);
	hobj->size = init_memory_pool(size, mem);
	if (hobj->size == (size_t)-1)
		return __bt(-EINVAL);

	return 0;
}

int heapobj_init_array_private(struct heapobj *hobj, const char *name,
			       size_t size, int elems)
{
	size_t poolsz;

	poolsz = (size + TLSF_BLOCK_ALIGN - 1) & ~(TLSF_BLOCK_ALIGN - 1);
	poolsz *= elems;

	return __bt(__heapobj_init_private(hobj, name, poolsz, NULL));
}

int heapobj_pkg_init_private(void)
{
	size_t size;
	void *mem;

	/*
	 * We want to know how many bytes from a memory pool TLSF will
	 * use for its own internal use. We get the probe memory from
	 * tlsf_malloc(), so that the main pool will be set up in the
	 * same move.
	 *
	 * We include 1k of additional memory to cope with the
	 * per-block overhead for an undefined number of individual
	 * allocation requests. Ugly.
	 */
	mem = tlsf_malloc(__copperplate_setup_data.mem_pool);
	size = init_memory_pool(__copperplate_setup_data.mem_pool, mem);
	if (size == (size_t)-1)
		panic("cannot initialize TLSF memory manager");

	destroy_memory_pool(mem);
	tlsf_pool_overhead = __copperplate_setup_data.mem_pool - size;
	tlsf_pool_overhead = (tlsf_pool_overhead + 1024) & ~15;
	tlsf_free(mem);

	return 0;
}

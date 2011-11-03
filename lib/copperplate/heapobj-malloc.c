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

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include "copperplate/wrappers.h"
#include "copperplate/init.h"
#include "copperplate/heapobj.h"
#include "copperplate/debug.h"

#define MALLOC_BLOCK_OVERHEAD  8

void __heapobj_destroy(struct heapobj *hobj)
{
}

int __heapobj_extend(struct heapobj *hobj, size_t size, void *mem)
{
	return 0;
}

void *__heapobj_alloc(struct heapobj *hobj, size_t size)
{
	/*
	 * XXX: We don't want debug _nrt assertions to trigger when
	 * running over Cobalt if the user picked this allocator, so
	 * we make sure to call the glibc directly, not the Cobalt
	 * wrappers.
	 */
	return __STD(malloc(size));
}

void __heapobj_free(struct heapobj *hobj, void *ptr)
{
	__STD(free(ptr));
}

size_t __heapobj_validate(struct heapobj *hobj, void *ptr)
{
	return malloc_usable_size(ptr);
}

size_t __heapobj_inquire(struct heapobj *hobj)
{
	struct mallinfo m = mallinfo();
	return m.uordblks;
}

#ifdef CONFIG_XENO_PSHARED

static struct heapobj_ops malloc_ops = {
	.destroy = __heapobj_destroy,
	.extend = __heapobj_extend,
	.alloc = __heapobj_alloc,
	.free = __heapobj_free,
	.validate = __heapobj_validate,
	.inquire = __heapobj_inquire,
};

#endif

void *pvmalloc(size_t size)
{
	return __STD(malloc(size));
}

void pvfree(void *ptr)
{
	__STD(free(ptr));
}

char *pvstrdup(const char *ptr)
{
	return strdup(ptr);
}

int heapobj_pkg_init_private(void)
{
	return 0;
}

int heapobj_init_private(struct heapobj *hobj, const char *name,
			 size_t size, void *mem)
{
	/*
	 * There is no local pool when working with malloc, we just
	 * use the global process arena. This should not be an issue
	 * since this mode is aimed at debugging, particularly to be
	 * used along with Valgrind.
	 */
#ifdef CONFIG_XENO_PSHARED
	hobj->ops = &malloc_ops;
#endif
	hobj->pool = mem;	/* Never used. */
	hobj->size = size;
	if (name)
		snprintf(hobj->name, sizeof(hobj->name), "%s", name);
	else
		snprintf(hobj->name, sizeof(hobj->name), "%p", hobj);

	return 0;
}

int heapobj_init_array_private(struct heapobj *hobj, const char *name,
			       size_t size, int elems)
{
	size += MALLOC_BLOCK_OVERHEAD;
	return __bt(heapobj_init_private(hobj, name, size * elems, NULL));
}

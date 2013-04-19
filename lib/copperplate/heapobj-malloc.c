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
#include <string.h>
#include <malloc.h>
#include "copperplate/heapobj.h"
#include "copperplate/debug.h"

int __heapobj_init_private(struct heapobj *hobj, const char *name,
			   size_t size, void *mem)
{
	/*
	 * There is no local pool when working with malloc, we just
	 * use the global process arena. This should not be an issue
	 * since this mode is aimed at debugging, particularly to be
	 * used along with Valgrind.
	 */
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
	return __bt(__heapobj_init_private(hobj, name, size * elems, NULL));
}

int heapobj_pkg_init_private(void)
{
	return 0;
}

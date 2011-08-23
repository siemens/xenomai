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

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <copperplate/lock.h>
#include <copperplate/heapobj.h>
#include <copperplate/panic.h>
#include <vxworks/errnoLib.h>
#include <vxworks/memPartLib.h>
#include "memPartLib.h"

#define mempart_magic	0x5a6b7c8d

static struct wind_mempart *find_mempart_from_id(PART_ID partId)
{
	struct wind_mempart *mp = mainheap_deref(partId, struct wind_mempart);

	if (mp == NULL || ((uintptr_t)mp & (sizeof(uintptr_t)-1)) != 0 ||
	    mp->magic != mempart_magic)
		return NULL;
	/*
	 * XXX: memory partitions may not be deleted, so we don't need
	 * to protect against references to stale objects.
	 */
	return mp;
}

PART_ID memPartCreate(char *pPool, unsigned int poolSize)
{
	struct wind_mempart *mp;
	struct service svc;

	COPPERPLATE_PROTECT(svc);

	mp = xnmalloc(sizeof(*mp));
	if (mp == NULL)
		goto fail;

	if (heapobj_init(&mp->hobj, NULL, poolSize, pPool)) {
		xnfree(mp);
	fail:
		errno = S_memLib_NOT_ENOUGH_MEMORY;
		COPPERPLATE_UNPROTECT(svc);
		return (PART_ID)0;
	}

	mp->magic = mempart_magic;

	COPPERPLATE_UNPROTECT(svc);

	return mainheap_ref(mp, PART_ID);
}

STATUS memPartAddToPool(PART_ID partId,
			char *pPool, unsigned int poolSize)
{
	struct wind_mempart *mp;
	struct service svc;
	STATUS ret;

	if (poolSize == 0) {
		errno = S_memLib_INVALID_NBYTES;
		return ERROR;
	}

	mp = find_mempart_from_id(partId);
	if (mp == NULL) {
		errno = S_objLib_OBJ_ID_ERROR;
		return ERROR;
	}

	COPPERPLATE_PROTECT(svc);

	if (heapobj_extend(&mp->hobj, poolSize, pPool)) {
		errno = S_memLib_INVALID_NBYTES;
		ret = ERROR;
	}

	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

void *memPartAlignedAlloc(PART_ID partId,
			  unsigned int nBytes, unsigned int alignment)
{
	unsigned int xtra = 0;
	void *ptr;

	/*
	 * XXX: We assume that our underlying allocator (TLSF, pshared
	 * or Glibc's malloc()) aligns at worst on a 8-bytes boundary,
	 * so we only have to care for larger constraints.
	 */
	if ((alignment & (alignment - 1)) != 0) {
		warning("%s: alignment value '%u' is not a power of two",
			__FUNCTION__, alignment);
		alignment = 8;
	}
	else if (alignment > 8)
		xtra = alignment;

	ptr = memPartAlloc(partId, nBytes + xtra);
	if (ptr == NULL)
		return NULL;

	return (void *)(((uintptr_t)ptr + xtra) & ~(alignment - 1));
}

void *memPartAlloc(PART_ID partId, unsigned int nBytes)
{
	struct wind_mempart *mp;

	if (nBytes == 0)
		return NULL;

	mp = find_mempart_from_id(partId);
	if (mp == NULL)
		return NULL;

	return heapobj_alloc(&mp->hobj, nBytes);
}

STATUS memPartFree(PART_ID partId, char *pBlock)
{
	struct wind_mempart *mp;
	struct service svc;

	if (pBlock == NULL)
		return ERROR;

	mp = find_mempart_from_id(partId);
	if (mp == NULL)
		return ERROR;

	COPPERPLATE_PROTECT(svc);

	heapobj_free(&mp->hobj, pBlock);

	COPPERPLATE_UNPROTECT(svc);

	return OK;
}

void memAddToPool(char *pPool, unsigned int poolSize)
{
	/*
	 * XXX: Since Glibc's malloc() is at least as efficient as
	 * VxWork's first-fit allocator, we just route allocation
	 * requests on the main partition to the regular malloc() and
	 * free() routines. Given that, our main pool is virtually
	 * infinite already, so we just give a hint to the user about
	 * this when asked to extend it.
	 */
	warning("%s: extending the main partition is useless", __FUNCTION__);
}

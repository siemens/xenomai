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

#include <errno.h>
#include <string.h>
#include <copperplate/threadobj.h>
#include <copperplate/heapobj.h>
#include "reference.h"
#include "internal.h"
#include "heap.h"
#include "timer.h"

struct syncluster alchemy_heap_table;

static struct alchemy_namegen heap_namegen = {
	.prefix = "heap",
	.length = sizeof ((struct alchemy_heap *)0)->name,
};

static struct alchemy_heap *get_alchemy_heap(RT_HEAP *heap,
					     struct syncstate *syns, int *err_r)
{
	struct alchemy_heap *hcb;

	if (heap == NULL || ((intptr_t)heap & (sizeof(intptr_t)-1)) != 0)
		goto bad_handle;

	hcb = mainheap_deref(heap->handle, struct alchemy_heap);
	if (hcb == NULL || ((intptr_t)hcb & (sizeof(intptr_t)-1)) != 0)
		goto bad_handle;

	if (hcb->magic != heap_magic)
		goto bad_handle;

	if (syncobj_lock(&hcb->sobj, syns))
		goto bad_handle;

	/* Recheck under lock. */
	if (hcb->magic == heap_magic)
		return hcb;
bad_handle:
	*err_r = -EINVAL;

	return NULL;
}

static inline void put_alchemy_heap(struct alchemy_heap *hcb,
				    struct syncstate *syns)
{
	syncobj_unlock(&hcb->sobj, syns);
}

static void heap_finalize(struct syncobj *sobj)
{
	struct alchemy_heap *hcb;
	hcb = container_of(sobj, struct alchemy_heap, sobj);
	heapobj_destroy(&hcb->hobj);
	xnfree(hcb);
}
fnref_register(libalchemy, heap_finalize);

int rt_heap_create(RT_HEAP *heap,
		   const char *name, size_t heapsize, int mode)
{
	struct alchemy_heap *hcb;
	int sobj_flags = 0, ret;
	struct service svc;

	if (threadobj_irq_p())
		return -EPERM;

	if (heapsize == 0)
		return -EINVAL;

	COPPERPLATE_PROTECT(svc);

	ret = -ENOMEM;
	hcb = xnmalloc(sizeof(*hcb));
	if (hcb == NULL)
		goto out;

	/*
	 * The memory pool has to be part of the main heap for proper
	 * sharing between processes.
	 */
	if (heapobj_init_shareable(&hcb->hobj, NULL, heapsize)) {
		xnfree(hcb);
		goto out;
	}

	alchemy_build_name(hcb->name, name, &heap_namegen);
	hcb->magic = heap_magic;
	hcb->mode = mode;
	hcb->size = heapsize;
	hcb->sba = NULL;

	if (mode & H_PRIO)
		sobj_flags = SYNCOBJ_PRIO;

	syncobj_init(&hcb->sobj, sobj_flags,
		     fnref_put(libalchemy, heap_finalize));

	ret = 0;
	if (syncluster_addobj(&alchemy_heap_table, hcb->name, &hcb->cobj)) {
		syncobj_uninit(&hcb->sobj);
		heapobj_destroy(&hcb->hobj);
		xnfree(hcb);
		ret = -EEXIST;
	} else
		heap->handle = mainheap_ref(hcb, uintptr_t);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_heap_delete(RT_HEAP *heap)
{
	struct alchemy_heap *hcb;
	struct syncstate syns;
	struct service svc;
	int ret = 0;

	if (threadobj_irq_p())
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	hcb = get_alchemy_heap(heap, &syns, &ret);
	if (hcb == NULL)
		goto out;

	syncluster_delobj(&alchemy_heap_table, &hcb->cobj);
	hcb->magic = ~heap_magic;
	syncobj_destroy(&hcb->sobj, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_heap_alloc(RT_HEAP *heap,
		  size_t size, RTIME timeout, void **blockp)
{
	struct alchemy_heap_wait *wait;
	struct timespec ts, *timespec;
	struct alchemy_heap *hcb;
	struct syncstate syns;
	struct service svc;
	void *p = NULL;
	int ret = 0;

	if (timeout != TM_NONBLOCK && !threadobj_current_p())
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	hcb = get_alchemy_heap(heap, &syns, &ret);
	if (hcb == NULL)
		goto out;

	if (hcb->mode & H_SINGLE) {
		p = hcb->sba;
		if (p)
			goto done;
		if (size > 0 && size != hcb->size) {
			ret = -EINVAL;
			goto done;
		}
		p = heapobj_alloc(&hcb->hobj, size);
		if (p == NULL) {
			ret = -ENOMEM;
			goto done;
		}
		hcb->sba = p;
		goto done;
	}

	p = heapobj_alloc(&hcb->hobj, size);
	if (p)
		goto done;

	if (timeout == TM_NONBLOCK) {
		ret = -EWOULDBLOCK;
		goto done;
	}

	timespec = alchemy_get_timespec(timeout, &ts);

	wait = threadobj_prepare_wait(struct alchemy_heap_wait);
	wait->size = size;

	ret = syncobj_pend(&hcb->sobj, timespec, &syns);
	if (ret) {
		if (ret == -EIDRM) {
			threadobj_finish_wait();
			goto out;
		}
	} else
		p = wait->ptr;

	threadobj_finish_wait();
done:
	*blockp = p;

	put_alchemy_heap(hcb, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_heap_free(RT_HEAP *heap, void *block)
{
	struct alchemy_heap_wait *wait;
	struct threadobj *thobj, *tmp;
	struct alchemy_heap *hcb;
	struct syncstate syns;
	struct service svc;
	int ret = 0;

	COPPERPLATE_PROTECT(svc);

	hcb = get_alchemy_heap(heap, &syns, &ret);
	if (hcb == NULL)
		goto out;

	if (hcb->mode & H_SINGLE)
		goto done;

	if (heapobj_validate(&hcb->hobj, block) == 0) {
		ret = -EINVAL;
		goto done;
	}

	heapobj_free(&hcb->hobj, block);

	if (!syncobj_pended_p(&hcb->sobj))
		goto done;
	/*
	 * We might be releasing a block large enough to satisfy
	 * multiple requests, so we iterate over all waiters.
	 */
	syncobj_for_each_waiter_safe(&hcb->sobj, thobj, tmp) {
		wait = threadobj_get_wait(thobj);
		wait->ptr = heapobj_alloc(&hcb->hobj, wait->size);
		if (wait->ptr)
			syncobj_wakeup_waiter(&hcb->sobj, thobj);
	}
done:
	put_alchemy_heap(hcb, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_heap_inquire(RT_HEAP *heap, RT_HEAP_INFO *info)
{
	struct alchemy_heap *hcb;
	struct syncstate syns;
	struct service svc;
	int ret = 0;

	COPPERPLATE_PROTECT(svc);

	hcb = get_alchemy_heap(heap, &syns, &ret);
	if (hcb == NULL)
		goto out;

	info->nwaiters = syncobj_pend_count(&hcb->sobj);
	info->heapsize = hcb->size;
	info->usablemem = heapobj_size(&hcb->hobj);
	info->usedmem = heapobj_inquire(&hcb->hobj);
	strcpy(info->name, hcb->name);

	put_alchemy_heap(hcb, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_heap_bind(RT_HEAP *heap,
		  const char *name, RTIME timeout)
{
	return alchemy_bind_object(name,
				   &alchemy_heap_table,
				   timeout,
				   offsetof(struct alchemy_heap, cobj),
				   &heap->handle);
}

int rt_heap_unbind(RT_HEAP *heap)
{
	heap->handle = 0;
	return 0;
}

/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Julien Pinon <jpinon@idealx.com>.
 * Copyright (C) 2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "vrtx/task.h"
#include "vrtx/heap.h"

static xnqueue_t vrtxheapq;

static void heap_destroy_internal(vrtxheap_t *heap);

int vrtxheap_init (u_long heap0size)

{
    char *heap0addr;
    int err;
    int heapid;

    initq(&vrtxheapq);

    if (heap0size < 2048)
	heap0size = 2048;

    heap0addr = (char *)xnmalloc(heap0size);

    if (!heap0addr)
	return -ENOMEM;

    heapid = sc_hcreate(heap0addr, heap0size, 7, &err);
    if ( err != RET_OK )
	{
	if ( err == ER_IIP)
	    {
	    return -EINVAL;
	    }
	else
	    {
	    return -ENOMEM;
	    }
	}
  
    return 0;
}

void vrtxheap_cleanup (void)

{
    xnholder_t *holder;

    while ((holder = getheadq(&vrtxheapq)) != NULL)
	heap_destroy_internal(link2vrtxheap(holder));
}

static void heap_destroy_internal (vrtxheap_t *heap)

{
    spl_t s;

    xnlock_get_irqsave(&nklock,s);
    removeq(&vrtxheapq,&heap->link);
    vrtx_mark_deleted(heap);
    xnlock_put_irqrestore(&nklock,s);
    xnheap_destroy(&heap->sysheap,NULL,NULL);
}


int sc_hcreate(char *heapaddr,
	       u_long heapsize,
	       unsigned log2psize,
	       int *perr)
{
    u_long pagesize;
    vrtxheap_t *heap;
    spl_t s;

    int err;
    int heapid;


    *perr = RET_OK;

    /* checks will be done in xnheap_init */

    if (log2psize == 0)
	{
	pagesize = 512; /* VRTXsa system call reference */
	}
    else
	{
	pagesize = 1 << log2psize;
	}


    heap = (vrtxheap_t *)xnmalloc(sizeof(*heap));

    if (!heap)
	{
	*perr = ER_NOCB;
	return 0;
	}

    err = xnheap_init(&heap->sysheap, heapaddr, heapsize, pagesize);
    if (err != 0)
	{
	    if (err == -EINVAL)
	        {
		*perr = ER_IIP;
		}
	    else
	        {
		*perr = ER_NOCB;
		}
	    xnfree(heap);

	    return 0;
	}


    heap->magic = VRTX_HEAP_MAGIC;
    inith(&heap->link);
    heap->log2psize = log2psize;
    heap->allocated = 0;
    heap->released = 0;

    xnlock_get_irqsave(&nklock,s);
    heapid = vrtx_alloc_id(heap);
    appendq(&vrtxheapq, &heap->link);
    xnlock_put_irqrestore(&nklock,s);

    return heapid;
}

void sc_hdelete (int hid, int opt, int *errp)
{
    vrtxheap_t *heap;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    heap = (vrtxheap_t *)vrtx_find_object_by_id(hid);
    
    if (opt == 0)
	{ /* delete heap only if no blocks are allocated */
	if (heap->sysheap.ubytes > 0)
	    {
	    xnlock_put_irqrestore(&nklock,s);
	    *errp = ER_PND;
	    return;
	    }
	}
    else if (opt != 1)
	{
	xnlock_put_irqrestore(&nklock,s);
	*errp = ER_IIP;
	return;
	}

    *errp = RET_OK;

    heap_destroy_internal(heap);

    xnlock_put_irqrestore(&nklock,s);
}

char *sc_halloc(int hid, unsigned long bsize, int *errp)
{
    vrtxheap_t *heap;
    char *blockp;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    heap = (vrtxheap_t *)vrtx_find_object_by_id(hid);
    if (heap == NULL)
	{
	xnlock_put_irqrestore(&nklock,s);
	*errp = ER_ID;
	return NULL;
	}

    blockp = xnheap_alloc(&heap->sysheap,bsize);
    if (blockp == NULL)
	{
	*errp = ER_MEM;
	}
    else
	{
	*errp = RET_OK;
	}
    heap->allocated++;

    xnlock_put_irqrestore(&nklock,s);

    return blockp;
}    

void sc_hfree(int hid, char *blockp, int *errp)
{
    vrtxheap_t *heap;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    heap = (vrtxheap_t *)vrtx_find_object_by_id(hid);

    if (heap == NULL)
	{
	xnlock_put_irqrestore(&nklock,s);
	*errp = ER_ID;
	return;
	}

    if (0 != xnheap_free(&heap->sysheap, blockp))
	{
	xnlock_put_irqrestore(&nklock,s);
	*errp = ER_NMB;
	return;
	}

    *errp = RET_OK;
    heap->allocated--;
    heap->released++;

    xnlock_put_irqrestore(&nklock,s);
}

void sc_hinquiry (int info[3], int hid, int *errp)
{
    vrtxheap_t *heap;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    heap = (vrtxheap_t *)vrtx_find_object_by_id(hid);
    if (heap == NULL)
	{
	xnlock_put_irqrestore(&nklock,s);
	*errp = ER_ID;
	return;
	}
    else
	{
	*errp = RET_OK;
	}

    info[0] = heap->allocated;

    info[1] = heap->released;

    info[2] = heap->log2psize;

    xnlock_put_irqrestore(&nklock,s);
}

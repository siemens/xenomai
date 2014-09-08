/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */
#ifndef _COBALT_KERNEL_HEAP_H
#define _COBALT_KERNEL_HEAP_H

#include <cobalt/kernel/lock.h>
#include <cobalt/kernel/list.h>
#include <cobalt/uapi/kernel/types.h>
#include <cobalt/uapi/kernel/heap.h>

/**
 * @addtogroup cobalt_core_heap
 * @{
 *
 * @par Implementation constraints
 *
 * - Minimum page size is 2 ** XNHEAP_MINLOG2 (must be large enough to
 * hold a pointer).
 *
 * - Maximum page size is 2 ** XNHEAP_MAXLOG2.
 *
 * - Minimum block size equals the minimum page size.
 *
 * - Requested block size smaller than the minimum block size is
 * rounded to the minimum block size.
 *
 * - Requested block size larger than 2 times the page size is rounded
 * to the next page boundary and obtained from the free page list. So
 * we need a bucket for each power of two between XNHEAP_MINLOG2 and
 * XNHEAP_MAXLOG2 inclusive, plus one to honor requests ranging from
 * the maximum page size to twice this size.
 */
#define XNHEAP_PAGESZ	  PAGE_SIZE
#define XNHEAP_MINLOG2    3
#define XNHEAP_MAXLOG2    22	/* Must hold pagemap::bcount objects */
#define XNHEAP_MINALLOCSZ (1 << XNHEAP_MINLOG2)
#define XNHEAP_MINALIGNSZ (1 << 4) /* i.e. 16 bytes */
#define XNHEAP_NBUCKETS   (XNHEAP_MAXLOG2 - XNHEAP_MINLOG2 + 2)
#define XNHEAP_MAXHEAPSZ  (1 << 31) /* i.e. 2Gb */

#define XNHEAP_PFREE   0
#define XNHEAP_PCONT   1
#define XNHEAP_PLIST   2

struct xnpagemap {
	unsigned int type : 8;	  /* PFREE, PCONT, PLIST or log2 */
	unsigned int bcount : 24; /* Number of active blocks. */
};

struct xnheap {
	char name[XNOBJECT_NAME_LEN];
	unsigned long size;
	unsigned long used;
	DECLARE_XNLOCK(lock);

	struct xnbucket {
		caddr_t freelist;
		int fcount;
	} buckets[XNHEAP_NBUCKETS];

	/** Base address of the page array */
	caddr_t membase;
	/** Memory limit of page array */
	caddr_t memlim;
	/** Number of pages in the freelist */
	unsigned long npages;
	/** Head of the free page list */
	caddr_t freelist;
	/** Address of the page map */
	struct xnpagemap *pagemap;
	/** heapq */
	struct list_head next;
};

extern struct xnheap kheap;

#define xnmalloc(size)     xnheap_alloc(&kheap, size)
#define xnfree(ptr)        xnheap_free(&kheap, ptr)

static inline size_t xnheap_get_size(const struct xnheap *heap)
{
	return heap->size;
}

static inline size_t xnheap_get_free(const struct xnheap *heap)
{
	return heap->size - heap->used;
}

static inline void *xnheap_get_membase(const struct xnheap *heap)
{
	return heap->membase;
}

static inline size_t xnheap_rounded_size(size_t size)
{
	if (size < 2 * XNHEAP_PAGESZ)
		return 2 * XNHEAP_PAGESZ;

	return ALIGN(size, XNHEAP_PAGESZ);
}

/* Private interface. */

#ifdef CONFIG_XENO_OPT_VFILE
void xnheap_init_proc(void);
void xnheap_cleanup_proc(void);
#else /* !CONFIG_XENO_OPT_VFILE */
static inline void xnheap_init_proc(void) { }
static inline void xnheap_cleanup_proc(void) { }
#endif /* !CONFIG_XENO_OPT_VFILE */

/* Public interface. */

int xnheap_init(struct xnheap *heap,
		void *membase,
		unsigned long size);

void xnheap_set_name(struct xnheap *heap,
		     const char *name, ...);

void xnheap_destroy(struct xnheap *heap,
		    void (*flushfn)(struct xnheap *heap,
				    void *membase,
				    unsigned long size,
				    void *cookie),
		    void *cookie);

void *xnheap_alloc(struct xnheap *heap,
		   unsigned long size);

void xnheap_free(struct xnheap *heap,
		void *block);

int xnheap_check_block(struct xnheap *heap,
		       void *block);

/** @} */

#endif /* !_COBALT_KERNEL_HEAP_H */

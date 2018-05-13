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
#ifndef _BOILERPLATE_HEAPMEM_H
#define _BOILERPLATE_HEAPMEM_H

#include <sys/types.h>
#include <stdint.h>
#include <limits.h>
#include <boilerplate/list.h>
#include <boilerplate/lock.h>
#include <boilerplate/avl.h>

#define HEAPMEM_PAGE_SHIFT	9 /* 2^9 => 512 bytes */
#define HEAPMEM_PAGE_SIZE	(1UL << HEAPMEM_PAGE_SHIFT)
#define HEAPMEM_PAGE_MASK	(~(HEAPMEM_PAGE_SIZE - 1))
#define HEAPMEM_MIN_LOG2	4 /* 16 bytes */
/*
 * Use bucketed memory for sizes between 2^HEAPMEM_MIN_LOG2 and
 * 2^(HEAPMEM_PAGE_SHIFT-1).
 */
#define HEAPMEM_MAX		(HEAPMEM_PAGE_SHIFT - HEAPMEM_MIN_LOG2)
#define HEAPMEM_MIN_ALIGN	(1U << HEAPMEM_MIN_LOG2)
/* Max size of an extent (4Gb - HEAPMEM_PAGE_SIZE). */
#define HEAPMEM_MAX_EXTSZ	(4294967295U - HEAPMEM_PAGE_SIZE + 1)
/* Bits we need for encoding a page # */
#define HEAPMEM_PGENT_BITS      (32 - HEAPMEM_PAGE_SHIFT)

/* Each page is represented by a page map entry. */
#define HEAPMEM_PGMAP_BYTES	sizeof(struct heapmem_pgentry)

struct heapmem_pgentry {
	/* Linkage in bucket list. */
	unsigned int prev : HEAPMEM_PGENT_BITS;
	unsigned int next : HEAPMEM_PGENT_BITS;
	/*  page_list or log2. */
	unsigned int type : 6;
	/*
	 * We hold either a spatial map of busy blocks within the page
	 * for bucketed memory (up to 32 blocks per page), or the
	 * overall size of the multi-page block if entry.type ==
	 * page_list.
	 */
	union {
		uint32_t map;
		uint32_t bsize;
	};
};

/*
 * A range descriptor is stored at the beginning of the first page of
 * a range of free pages. heapmem_range.size is nrpages *
 * HEAPMEM_PAGE_SIZE. Ranges are indexed by address and size in AVL
 * trees.
 */
struct heapmem_range {
	struct avlh addr_node;
	struct avlh size_node;
	size_t size;
};

struct heapmem_extent {
	struct pvholder next;
	void *membase;		/* Base of page array */
	void *memlim;		/* Limit of page array */
	struct avl addr_tree;
	struct avl size_tree;
	struct heapmem_pgentry pagemap[0]; /* Start of page entries[] */
};

struct heap_memory {
	pthread_mutex_t lock;
	struct pvlistobj extents;
	size_t arena_size;
	size_t usable_size;
	size_t used_size;
	/* Heads of page lists for log2-sized blocks. */
	uint32_t buckets[HEAPMEM_MAX];
};

#define __HEAPMEM_MAP_SIZE(__nrpages)					\
	((__nrpages) * HEAPMEM_PGMAP_BYTES)

#define __HEAPMEM_ARENA_SIZE(__size)					\
	(__size +							\
	 __align_to(sizeof(struct heapmem_extent) +			\
		    __HEAPMEM_MAP_SIZE((__size) >> HEAPMEM_PAGE_SHIFT),	\
		    HEAPMEM_MIN_ALIGN))

/*
 * Calculate the minimal size of the memory arena needed to contain a
 * heap of __user_size bytes, including our meta data for managing it.
 * Usable at build time if __user_size is constant.
 */
#define HEAPMEM_ARENA_SIZE(__user_size)					\
	__HEAPMEM_ARENA_SIZE(__align_to(__user_size, HEAPMEM_PAGE_SIZE))

#ifdef __cplusplus
extern "C" {
#endif

int heapmem_init(struct heap_memory *heap,
		 void *mem, size_t size);

int heapmem_extend(struct heap_memory *heap,
		   void *mem, size_t size);

void heapmem_destroy(struct heap_memory *heap);

void *heapmem_alloc(struct heap_memory *heap,
		    size_t size) __alloc_size(2);

int heapmem_free(struct heap_memory *heap,
		 void *block);

static inline
size_t heapmem_arena_size(const struct heap_memory *heap)
{
	return heap->arena_size;
}

static inline
size_t heapmem_usable_size(const struct heap_memory *heap)
{
	return heap->usable_size;
}

static inline
size_t heapmem_used_size(const struct heap_memory *heap)
{
	return heap->used_size;
}

ssize_t heapmem_check(struct heap_memory *heap,
		      void *block);

#ifdef __cplusplus
}
#endif

#endif /* _BOILERPLATE_HEAPMEM_H */

/*
 * Copyright (C) 2010 Philippe Gerum <rpm@xenomai.org>.
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
 *
 * This code is adapted from Xenomai's original dual kernel xnheap
 * support. It is simple and efficient enough for managing dynamic
 * memory allocation backed by a tmpfs file, we can share between
 * multiple processes in user-space.
 */

/*
 * XXX: the policy for handling caller cancellation is to disable
 * asynchronous cancellation in critical sections altering the heap
 * state. The rationale is twofold:
 *
 * - undoing the state changes involved in allocating/freeing blocks
 * from cleanup handlers would be extremely complex.
 *
 * - the critical sections traverse no cancellation point.
 *
 * Therefore we simply set the cancellation type to deferred mode for
 * the caller in routines which alter the heap state.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include "copperplate/init.h"
#include "copperplate/lock.h"
#include "copperplate/list.h"
#include "copperplate/hash.h"
#include "copperplate/heapobj.h"

#include <linux/unistd.h>
#define do_gettid()	syscall(__NR_gettid)

#define HOBJ_PAGE_SHIFT	9	/* 2^9 => 512 bytes */
#define HOBJ_PAGE_SIZE		(1U << HOBJ_PAGE_SHIFT)
#define HOBJ_PAGE_MASK		(~(HOBJ_PAGE_SIZE-1))
#define HOBJ_PAGE_ALIGN(addr)	(((addr)+HOBJ_PAGE_SIZE-1)&HOBJ_PAGE_MASK)

#define HOBJ_MINLOG2    3
#define HOBJ_MAXLOG2    22	/* Must hold pagemap::bcount objects */
#define HOBJ_MINALIGNSZ (1U << 4) /* i.e. 16 bytes */
#define HOBJ_NBUCKETS   (HOBJ_MAXLOG2 - HOBJ_MINLOG2 + 2)
#define HOBJ_MAXEXTSZ   (1U << 31) /* i.e. 2Gb */

/*
 * The base address of the shared memory segment, as seen by each
 * individual process.
 */
void *__pshared_heap;

struct hash_table *__pshared_catalog;

enum {
	page_free =0,
	page_cont =1,
	page_list =2
};

struct page_map {
	unsigned int type : 8;	  /* free, cont, list or log2 */
	unsigned int bcount : 24; /* Number of active blocks. */
};

struct heap_extent {
	struct holder link;
	off_t membase;	/* Base address of the page array */
	off_t memlim;	/* Memory limit of page array */
	off_t freelist;	/* Head of the free page list */
	struct page_map pagemap[1];	/* Start of page map */
};

/*
 * The struct below has to live in shared memory; no direct reference
 * to process local memory in there.
 */
struct heap {
	pthread_mutex_t lock;
	struct list extents;
	size_t extentsize;
	size_t hdrsize;
	size_t npages;
	size_t ubytes;
	size_t maxcont;
	struct {
		off_t freelist;
		int fcount;
	} buckets[HOBJ_NBUCKETS];
	int cpid;
	off_t maplen;
	struct hash_table catalog;
};

static struct heapobj main_pool;

#define __moff(h, p)		((caddr_t)(p) - (caddr_t)(h))
#define __moff_check(h, p)	((p) ? __moff(h, p) : 0)
#define __mref(h, o)  		((void *)((caddr_t)(h) + (o)))
#define __mref_check(h, o)	((o) ? __mref(h, o) : NULL)

static inline size_t __align_to(size_t size, size_t al)
{
	/* The alignment value must be a power of 2 */
	return ((size+al-1)&(~(al-1)));
}

static inline size_t internal_overhead(size_t hsize)
{
	/* o = (h - o) * m / p + e
	   o * p = (h - o) * m + e * p
	   o * (p + m) = h * m + e * p
	   o = (h * m + e *p) / (p + m)
	*/
	return __align_to((sizeof(struct heap_extent) * HOBJ_PAGE_SIZE
			   + sizeof(struct page_map) * hsize)
			  / (HOBJ_PAGE_SIZE + sizeof(struct page_map)), HOBJ_PAGE_SIZE);
}

static void init_extent(struct heap *heap, struct heap_extent *extent)
{
	caddr_t freepage;
	int n, lastpgnum;

	__holder_init(heap, &extent->link);

	/* The initial extent starts right after the header. */
	extent->membase = __moff(heap, extent) + heap->hdrsize;
	lastpgnum = heap->npages - 1;

	/* Mark each page as free in the page map. */
	for (n = 0, freepage = __mref(heap, extent->membase);
	     n < lastpgnum; n++, freepage += HOBJ_PAGE_SIZE) {
		*((off_t *)freepage) = __moff(heap, freepage) + HOBJ_PAGE_SIZE;
		extent->pagemap[n].type = page_free;
		extent->pagemap[n].bcount = 0;
	}

	*((off_t *)freepage) = 0;
	extent->pagemap[lastpgnum].type = page_free;
	extent->pagemap[lastpgnum].bcount = 0;
	extent->memlim = __moff(heap, freepage) + HOBJ_PAGE_SIZE;

	/* The first page starts the free list of a new extent. */
	extent->freelist = extent->membase;
}

static void init_heap(struct heap *heap, void *mem, size_t size)
{
	struct heap_extent *extent;
	pthread_mutexattr_t mattr;

	heap->extentsize = size;
	heap->hdrsize = internal_overhead(size);
	heap->npages = (size - heap->hdrsize) >> HOBJ_PAGE_SHIFT;

	/*
	 * An extent must contain at least two addressable pages to
	 * cope with allocation sizes between PAGESIZE and 2 *
	 * PAGESIZE.
	 */
	assert(heap->npages >= 2);

	heap->cpid = do_gettid();
	heap->ubytes = 0;
	heap->maxcont = heap->npages * HOBJ_PAGE_SIZE;
	__list_init(heap, &heap->extents);

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
	pthread_mutex_init(&heap->lock, &mattr);
	pthread_mutexattr_destroy(&mattr);

	memset(heap->buckets, 0, sizeof(heap->buckets));
	extent = mem;
	init_extent(heap, extent);

	__hash_init(heap, &heap->catalog);
	__list_append(heap, &extent->link, &heap->extents);
}

static caddr_t get_free_range(struct heap *heap, size_t bsize, int log2size)
{
	caddr_t block, eblock, freepage, lastpage, headpage, freehead = NULL;
	size_t pnum, pcont, fcont;
	struct heap_extent *extent;

	__list_for_each_entry(heap, extent, &heap->extents, link) {
		freepage = __mref_check(heap, extent->freelist);
		while (freepage) {
			headpage = freepage;
			fcont = 0;
			/*
			 * Search for a range of contiguous pages in
			 * the free page list of the current
			 * extent. The range must be 'bsize' long.
			 */
			do {
				lastpage = freepage;
				freepage = __mref_check(heap, *((off_t *)freepage));
				fcont += HOBJ_PAGE_SIZE;
			} while (freepage == lastpage + HOBJ_PAGE_SIZE
				 && fcont < bsize);
			if (fcont >= bsize) {
				/*
				 * Ok, got it. Just update the free
				 * page list, then proceed to the next
				 * step.
				 */
				if (__moff(heap, headpage) == extent->freelist)
					extent->freelist = *((off_t *)lastpage);
				else
					*((off_t *)freehead) = *((off_t *)lastpage);

				goto splitpage;
			}
			freehead = lastpage;
		}
	}

	return NULL;

splitpage:

	/*
	 * At this point, headpage is valid and points to the first page
	 * of a range of contiguous free pages larger or equal than
	 * 'bsize'.
	 */
	if (bsize < HOBJ_PAGE_SIZE) {
		/*
		 * If the allocation size is smaller than the standard page
		 * size, split the page in smaller blocks of this size,
		 * building a free list of free blocks.
		 */
		for (block = headpage, eblock =
		     headpage + HOBJ_PAGE_SIZE - bsize; block < eblock;
		     block += bsize)
			*((off_t *)block) = __moff(heap, block) + bsize;

		*((off_t *)eblock) = 0;
	} else
		*((off_t *)headpage) = 0;

	pnum = (__moff(heap, headpage) - extent->membase) >> HOBJ_PAGE_SHIFT;

	/*
	 * Update the page map.  If log2size is non-zero (i.e. bsize
	 * <= 2 * PAGESIZE), store it in the first page's slot to
	 * record the exact block size (which is a power of
	 * two). Otherwise, store the special marker page_list,
	 * indicating the start of a block whose size is a multiple of
	 * the standard page size, but not necessarily a power of two.
	 * In any case, the following pages slots are marked as
	 * 'continued' (page_cont).
	 */
	extent->pagemap[pnum].type = log2size ? : page_list;
	extent->pagemap[pnum].bcount = 1;

	for (pcont = bsize >> HOBJ_PAGE_SHIFT; pcont > 1; pcont--) {
		extent->pagemap[pnum + pcont - 1].type = page_cont;
		extent->pagemap[pnum + pcont - 1].bcount = 0;
	}

	return headpage;
}

static inline size_t  __attribute__ ((always_inline))
align_alloc_size(size_t size)
{
	/*
	 * Sizes greater than the page size are rounded to a multiple
	 * of the page size.
	 */
	if (size > HOBJ_PAGE_SIZE)
		return __align_to(size, HOBJ_PAGE_SIZE);

	return __align_to(size, HOBJ_MINALIGNSZ);
}

static void *alloc_block(struct heap *heap, size_t size)
{
	struct heap_extent *extent;
	int log2size, ilog;
	size_t pnum, bsize;
	caddr_t block;

	if (size == 0)
		return NULL;

	size = align_alloc_size(size);
	/*
	 * It becomes more space efficient to directly allocate pages from
	 * the free page list whenever the requested size is greater than
	 * 2 times the page size. Otherwise, use the bucketed memory
	 * blocks.
	 */
	if (size <= HOBJ_PAGE_SIZE * 2) {
		/*
		 * Find the first power of two greater or equal to the
		 * rounded size. The log2 value of this size is also
		 * computed.
		 */
		for (bsize = (1 << HOBJ_MINLOG2), log2size = HOBJ_MINLOG2;
		     bsize < size; bsize <<= 1, log2size++)
			;	/* Loop */

		ilog = log2size - HOBJ_MINLOG2;

		write_lock_nocancel(&heap->lock);

		block = __mref_check(heap, heap->buckets[ilog].freelist);
		if (block == NULL) {
			block = get_free_range(heap, bsize, log2size);
			if (block == NULL)
				goto done;
			if (bsize <= HOBJ_PAGE_SIZE)
				heap->buckets[ilog].fcount += (HOBJ_PAGE_SIZE >> log2size) - 1;
		} else {
			if (bsize <= HOBJ_PAGE_SIZE)
				--heap->buckets[ilog].fcount;

			/* Search for the source extent of block. */
			extent = NULL;
			__list_for_each_entry(heap, extent, &heap->extents, link) {
				if (__moff(heap, block) >= extent->membase &&
				    __moff(heap, block) < extent->memlim)
					break;
			}
			assert(extent != NULL);
			pnum = (__moff(heap, block) - extent->membase) >> HOBJ_PAGE_SHIFT;
			++extent->pagemap[pnum].bcount;
		}

		heap->buckets[ilog].freelist = *((off_t *)block);
		heap->ubytes += bsize;
	} else {
		if (size > heap->maxcont)
			return NULL;

		write_lock_nocancel(&heap->lock);

		/* Directly request a free page range. */
		block = get_free_range(heap, size, 0);
		if (block)
			heap->ubytes += size;
	}
done:
	write_unlock(&heap->lock);

	return block;
}

static int free_block(struct heap *heap, void *block)
{
	caddr_t freepage, lastpage, nextpage, tailpage, freeptr;
	int log2size, ret = 0, nblocks, xpage, ilog;
	size_t pnum, pcont, boffset, bsize, npages;
	struct heap_extent *extent = NULL;
	off_t *tailptr;

	write_lock_nocancel(&heap->lock);

	/*
	 * Find the extent from which the returned block is
	 * originating from.
	 */
	__list_for_each_entry(heap, extent, &heap->extents, link) {
		if (__moff(heap, block) >= extent->membase &&
		    __moff(heap, block) < extent->memlim)
			break;
	}

	if (extent == NULL) {
		ret = -EFAULT;
		goto out;
	}

	/* Compute the heading page number in the page map. */
	pnum = (__moff(heap, block) - extent->membase) >> HOBJ_PAGE_SHIFT;
	boffset = (__moff(heap, block) -
		   (extent->membase + (pnum << HOBJ_PAGE_SHIFT)));

	switch (extent->pagemap[pnum].type) {
	case page_free:	/* Unallocated page? */
	case page_cont:	/* Not a range heading page? */
		ret = -EINVAL;
		goto out;

	case page_list:
		npages = 1;
		while (npages < heap->npages &&
		       extent->pagemap[pnum + npages].type == page_cont)
			npages++;
		bsize = npages * HOBJ_PAGE_SIZE;

	free_page_list:
		/* Link all freed pages in a single sub-list. */
		for (freepage = (caddr_t)block,
		     tailpage = (caddr_t)block + bsize - HOBJ_PAGE_SIZE;
		     freepage < tailpage; freepage += HOBJ_PAGE_SIZE)
			*((off_t *)freepage) = __moff(heap, freepage) + HOBJ_PAGE_SIZE;

	free_pages:
		/* Mark the released pages as free in the extent's page map. */
		for (pcont = 0; pcont < npages; pcont++)
			extent->pagemap[pnum + pcont].type = page_free;
		/*
		 * Return the sub-list to the free page list, keeping
		 * an increasing address order to favor coalescence.
		 */
		for (nextpage = __mref_check(heap, extent->freelist), lastpage = NULL;
		     nextpage && nextpage < (caddr_t)block;
		     lastpage = nextpage, nextpage = __mref_check(heap, *((off_t *)nextpage)))
		  ;	/* Loop */

		*((off_t *)tailpage) = __moff_check(heap, nextpage);
		if (lastpage)
			*((off_t *)lastpage) = __moff(heap, block);
		else
			extent->freelist = __moff(heap, block);
		break;

	default:

		log2size = extent->pagemap[pnum].type;
		bsize = (1 << log2size);
		if ((boffset & (bsize - 1)) != 0) {	/* Not a block start? */
			ret = -EINVAL;
			goto out;
		}
		/*
		 * Return the page to the free list if we've just
		 * freed its last busy block. Pages from multi-page
		 * blocks are always pushed to the free list (bcount
		 * value for the heading page is always 1).
		 */
		ilog = log2size - HOBJ_MINLOG2;
		if (--extent->pagemap[pnum].bcount > 0) {
			/* Return the block to the bucketed memory space. */
			*((off_t *)block) = heap->buckets[ilog].freelist;
			heap->buckets[ilog].freelist = __moff(heap, block);
			++heap->buckets[ilog].fcount;
			break;
		}

		npages = bsize >> HOBJ_PAGE_SHIFT;
		if (npages > 1)
			/*
			 * The simplest case: we only have a single
			 * block to deal with, which spans multiple
			 * pages. We just need to release it as a list
			 * of pages, without caring about the
			 * consistency of the bucket.
			 */
			goto free_page_list;

		freepage = __mref(heap, extent->membase) + (pnum << HOBJ_PAGE_SHIFT);
		block = freepage;
		tailpage = freepage;
		nextpage = freepage + HOBJ_PAGE_SIZE;
		nblocks = HOBJ_PAGE_SIZE >> log2size;
		heap->buckets[ilog].fcount -= (nblocks - 1);
		assert(heap->buckets[ilog].fcount >= 0);

		/*
		 * Easy case: all free blocks are laid on a single
		 * page we are now releasing. Just clear the bucket
		 * and bail out.
		 */
		if (heap->buckets[ilog].fcount == 0) {
			heap->buckets[ilog].freelist = 0;
			goto free_pages;
		}

		/*
		 * Worst case: multiple pages are traversed by the
		 * bucket list. Scan the list to remove all blocks
		 * belonging to the freed page. We are done whenever
		 * all possible blocks from the freed page have been
		 * traversed, or we hit the end of list, whichever
		 * comes first.
		 */
		for (tailptr = &heap->buckets[ilog].freelist,
			     freeptr = __mref_check(heap, *tailptr), xpage = 1;
		     freeptr && nblocks > 0; freeptr = __mref_check(heap, *((off_t *)freeptr))) {
			if (freeptr < freepage || freeptr >= nextpage) {
				if (xpage) { /* Limit random writes */
					*tailptr = __moff(heap, freeptr);
					xpage = 0;
				}
				tailptr = (off_t *)freeptr;
			} else {
				--nblocks;
				xpage = 1;
			}
		}
		*tailptr = __moff_check(heap, freeptr);
		goto free_pages;
	}

	heap->ubytes -= bsize;
out:
	write_unlock(&heap->lock);

	return ret;
}

static size_t check_block(struct heap *heap, void *block)
{
	size_t pnum, boffset, bsize, ret = 0;
	struct heap_extent *extent = NULL;
	int ptype;

	read_lock_nocancel(&heap->lock);

	/*
	 * Find the extent the checked block is originating from.
	 */
	__list_for_each_entry(heap, extent, &heap->extents, link) {
		if (__moff(heap, block) >= extent->membase &&
		    __moff(heap, block) < extent->memlim)
			break;
	}
	if (extent == NULL)
		goto out;

	/* Compute the heading page number in the page map. */
	pnum = (__moff(heap, block) - extent->membase) >> HOBJ_PAGE_SHIFT;
	ptype = extent->pagemap[pnum].type;
	if (ptype == page_free || ptype == page_cont) {
		ret = -EINVAL;
		goto out;
	}

	bsize = (1 << ptype);
	boffset = (__moff(heap, block) -
		   (extent->membase + (pnum << HOBJ_PAGE_SHIFT)));
	if ((boffset & (bsize - 1)) != 0) /* Not a block start? */
		goto out;

	ret = bsize;
out:
	read_unlock(&heap->lock);

	return ret;
}

static void *realloc_block(struct heap *heap, void *block, size_t newsize)
{
	void *newblock;

	if (newsize == 0)
		return NULL;

	newblock = alloc_block(heap, newsize);
	if (newblock == NULL)
		return NULL;

	memcpy(newblock, block, newsize);
	free_block(heap, block);

	return newblock;
}

static int create_heap(struct heapobj *hobj, const char *session,
		       const char *name, size_t size, int force)
{
	struct heap *heap;
	struct stat sbuf;
	int ret, fd;
	off_t len;

	if (size <= sizeof(struct heap_extent))
		size = sizeof(struct heap_extent);
	else {
		size = __align_to(size, HOBJ_PAGE_SIZE);
		if (size > HOBJ_MAXEXTSZ)
			return -EINVAL;
	}

	if (size - sizeof(struct heap_extent) < HOBJ_PAGE_SIZE * 2)
		size += HOBJ_PAGE_SIZE * 2;

	if (name)
		snprintf(hobj->name, sizeof(hobj->name), "%s:%s", session, name);
	else
		snprintf(hobj->name, sizeof(hobj->name), "%s:%p", session, hobj);

	snprintf(hobj->fsname, sizeof(hobj->fsname), "/xeno:%s", hobj->name);
	len = size + sizeof(*heap);

	if (force)
		shm_unlink(hobj->fsname);

	fd = shm_open(hobj->fsname, O_RDWR|O_CREAT, 0600);
	if (fd < 0)
		return -errno;

	ret = flock(fd, LOCK_EX);
	if (ret) {
		close(fd);
		return -errno;
	}
		
	ret = fstat(fd, &sbuf);
	if (ret) {
		close(fd);
		return -errno;
	}

	if (sbuf.st_size == 0) {
		ret = ftruncate(fd, len);
		if (ret) {
			close(fd);
			shm_unlink(hobj->fsname);
			return -errno;
		}
	} else if (sbuf.st_size != len) {
		close(fd);
		return -EEXIST;
	}

	heap = mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (heap == MAP_FAILED) {
		close(fd);
		return -errno;
	}

	if (sbuf.st_size == 0 || (heap->cpid && kill(heap->cpid, 0))) {
		heap->maplen = len;
		init_heap(heap, (caddr_t)heap + sizeof(*heap), size);
	}

	flock(fd, LOCK_UN);

	hobj->pool = heap;
	hobj->size = size;
	hobj->fd = fd;

	return 0;
}

static void pshared_destroy(struct heapobj *hobj)
{
	struct heap *heap = hobj->pool;
	int cpid = heap->cpid;

	munmap(heap, hobj->size + sizeof(*heap));
	close(hobj->fd);

	if (cpid == do_gettid() || (cpid && kill(cpid, 0)))
		shm_unlink(hobj->fsname);
}

static int pshared_extend(struct heapobj *hobj, size_t size, void *mem)
{
	struct heap *heap = hobj->pool;
	struct heap_extent *extent;
	size_t newsize;
	int ret, state;
	caddr_t p;

	if (hobj == &main_pool)	/* Cannot extend the main pool. */
		return -EINVAL;

	if (size <= HOBJ_PAGE_SIZE * 2)
		return -EINVAL;

	newsize = size + hobj->size + sizeof(*heap) + sizeof(*extent);
	ret = ftruncate(hobj->fd, newsize);
	if (ret)
		return -errno;
	/*
	 * We do not allow the kernel to move the mapping address, so
	 * it is safe referring to the heap contents while extending
	 * it.
	 */
	write_lock_safe(&heap->lock, state);
	p = mremap(heap, hobj->size + sizeof(*heap), newsize, 0);
	if (p == MAP_FAILED) {
		ret = -errno;
		goto out;
	}

	extent = (struct heap_extent *)(p + hobj->size + sizeof(*heap));
	init_extent(heap, extent);
	__list_append(heap, &extent->link, &heap->extents);
	hobj->size = newsize - sizeof(*heap);
out:
	write_unlock_safe(&heap->lock, state);

	return ret;
}

static void *pshared_alloc(struct heapobj *hobj, size_t size)
{
	return alloc_block(hobj->pool, size);
}

static void *pshared_realloc(struct heapobj *hobj, void *ptr, size_t size)
{
	return realloc_block(hobj->pool, ptr, size);
}

static void pshared_free(struct heapobj *hobj, void *ptr)
{
	free_block(hobj->pool, ptr);
}

static size_t pshared_inquire(struct heapobj *hobj, void *ptr)
{
	return check_block(hobj->pool, ptr);
}

static struct heapobj_ops pshared_ops = {
	.destroy = pshared_destroy,
	.extend = pshared_extend,
	.alloc = pshared_alloc,
	.realloc = pshared_realloc,
	.free = pshared_free,
	.inquire = pshared_inquire,
};

int heapobj_init(struct heapobj *hobj, const char *name,
		 size_t size, void *mem)
{
	hobj->ops = &pshared_ops;
	return create_heap(hobj, __session_label_arg, name,
			   size, __reset_session_arg);
}

int heapobj_init_array(struct heapobj *hobj, const char *name,
		       size_t size, int elems)
{
	size = align_alloc_size(size);
	return heapobj_init(hobj, name, size * elems, NULL);
}

void *xnmalloc(size_t size)
{
	return alloc_block(&main_heap, size);
}

void *xnrealloc(void *ptr, size_t size)
{
	return realloc_block(&main_heap, ptr, size);
}

void xnfree(void *ptr)
{
	free_block(&main_heap, ptr);
}

char *xnstrdup(const char *ptr)
{
	char *str;

	str = xnmalloc(strlen(ptr) + 1);
	if (str == NULL)
		return NULL;

	return strcpy(str, ptr);
}

int heapobj_pkg_init_shared(void)
{
	int ret;

	ret = heapobj_init(&main_pool, "main", __mem_pool_arg, NULL);
	if (ret) {
		if (ret == -EEXIST) {
			if (__reset_session_arg)
				/* Init failed despite override. */
				warning("active session %s is conflicting\n",
					__session_label_arg);
			else
				warning("non-matching session %s already exists,\n"
					"pass --reset to override.",
					__session_label_arg);
		}

		return ret;
	}

	__pshared_heap = main_pool.pool;
	__pshared_catalog = &main_heap.catalog;

	return 0;
}

int pshared_check(void *__heap, void *__addr)
{
	struct heap *heap = __heap;
	return __addr >= __heap && __addr < __heap + heap->maplen;
}

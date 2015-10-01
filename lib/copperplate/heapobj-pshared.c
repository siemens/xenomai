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
#include <malloc.h>
#include <unistd.h>
#include "boilerplate/list.h"
#include "boilerplate/hash.h"
#include "boilerplate/lock.h"
#include "copperplate/heapobj.h"
#include "copperplate/debug.h"
#include "xenomai/init.h"
#include "internal.h"

#define HOBJ_PAGE_SHIFT	9	/* 2^9 => 512 bytes */
#define HOBJ_PAGE_SIZE		(1U << HOBJ_PAGE_SHIFT)
#define HOBJ_PAGE_MASK		(~(HOBJ_PAGE_SIZE-1))
#define HOBJ_PAGE_ALIGN(addr)	(((addr)+HOBJ_PAGE_SIZE-1)&HOBJ_PAGE_MASK)

#define HOBJ_MINALIGNSZ (1U << 4) /* i.e. 16 bytes */
#define HOBJ_MAXEXTSZ   (1U << 31) /* i.e. 2Gb */

enum {
	page_free =0,
	page_cont =1,
	page_list =2
};

struct page_entry {
	unsigned int type : 8;	  /* free, cont, list or log2 */
	unsigned int bcount : 24; /* Number of active blocks. */
};

struct shared_extent {
	struct holder link;
	memoff_t membase;	/* Base offset of page array */
	memoff_t memlim;	/* Offset limit of page array */
	memoff_t freelist;	/* Head of free page list */
	struct page_entry pagemap[1]; /* Start of page map */
};

/*
 * The main heap consists of a shared heap at its core, with
 * additional session-wide information.
 */
struct session_heap {
	struct shared_heap heap;
	int cpid;
	memoff_t maplen;
	struct hash_table catalog;
	struct sysgroup sysgroup;
};

/*
 * The base address of the shared memory heap, as seen by each
 * individual process. Its control block is always first, so that
 * different processes can access this information right after the
 * segment is mmapped. This also ensures that offset 0 will never
 * refer to a valid page or block.
 */
void *__main_heap;
#define main_heap	(*(struct session_heap *)__main_heap)
/*
 *  Base address for offset-based addressing, which is the start of
 *  the session heap since all memory objects are allocated from it,
 *  including other (sub-)heaps.
 */
#define main_base	__main_heap

/* A table of shared clusters for the session. */
struct hash_table *__main_catalog;

/* Pointer to the system list group. */
struct sysgroup *__main_sysgroup;

static struct heapobj main_pool;

#define __shoff(b, p)		((caddr_t)(p) - (caddr_t)(b))
#define __shoff_check(b, p)	((p) ? __shoff(b, p) : 0)
#define __shref(b, o)		((void *)((caddr_t)(b) + (o)))
#define __shref_check(b, o)	((o) ? __shref(b, o) : NULL)

static inline size_t __align_to(size_t size, size_t al)
{
	/* The alignment value must be a power of 2 */
	return ((size+al-1)&(~(al-1)));
}

static inline size_t get_pagemap_size(size_t h)
{
	/*
	 * Return the size of the meta data required to map 'h' bytes
	 * of user memory in pages of HOBJ_PAGE_SIZE bytes. The meta
	 * data includes the length of the extent descriptor, plus the
	 * length of the page mapping array. 'h' must be a multiple of
	 * HOBJ_PAGE_SIZE on entry.
	 */
	assert((h & ~HOBJ_PAGE_MASK) == 0);
	return __align_to((h >> HOBJ_PAGE_SHIFT) * sizeof(struct page_entry)
			  + sizeof(struct shared_extent), HOBJ_MINALIGNSZ);
}

static void init_extent(void *base, struct shared_extent *extent)
{
	caddr_t freepage;
	int n, lastpgnum;

	__holder_init_nocheck(base, &extent->link);

	lastpgnum = ((extent->memlim - extent->membase) >> HOBJ_PAGE_SHIFT) - 1;
	/*
	 * An extent must contain at least two addressable pages to
	 * cope with allocation sizes between PAGESIZE and 2 *
	 * PAGESIZE.
	 */
	assert(lastpgnum >= 1);

	/* Mark each page as free in the page map. */
	for (n = 0, freepage = __shref(base, extent->membase);
	     n < lastpgnum; n++, freepage += HOBJ_PAGE_SIZE) {
		*((memoff_t *)freepage) = __shoff(base, freepage) + HOBJ_PAGE_SIZE;
		extent->pagemap[n].type = page_free;
		extent->pagemap[n].bcount = 0;
	}

	*((memoff_t *)freepage) = 0;
	extent->pagemap[lastpgnum].type = page_free;
	extent->pagemap[lastpgnum].bcount = 0;

	/* The first page starts the free list of a new extent. */
	extent->freelist = extent->membase;
}

static int init_heap(struct shared_heap *heap, void *base,
		     const char *name,
		     void *mem, size_t size)
{
	struct shared_extent *extent;
	pthread_mutexattr_t mattr;
	int ret;

	namecpy(heap->name, name);

	heap->ubytes = 0;
	heap->total = size;
	heap->maxcont = heap->total;
	__list_init_nocheck(base, &heap->extents);

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, mutex_type_attribute);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
	ret = __bt(-__RT(pthread_mutex_init(&heap->lock, &mattr)));
	pthread_mutexattr_destroy(&mattr);
	if (ret)
		return ret;

	memset(heap->buckets, 0, sizeof(heap->buckets));

	/*
	 * The heap descriptor is followed in memory by the initial
	 * extent covering the 'size' bytes of user memory, which is a
	 * multiple of HOBJ_PAGE_SIZE. The extent starts with a
	 * descriptor, which is in turn followed by a page mapping
	 * array. The length of the page mapping array depends on the
	 * size of the user memory to map.
	 *
	 * +-------------------+
	 * |  heap descriptor  |
	 * +-------------------+
	 * | extent descriptor |
	 * /...................\
	 * \....(page map)...../
	 * /...................\
	 * +-------------------+ <= extent->membase
	 * |                   |
	 * |  (user memory)    |
	 * |                   |
	 * +-------------------+
	 *                       <= extent->memlim
	 */
	extent = mem;
	extent->membase = __shoff(base, mem) + get_pagemap_size(size);
	extent->memlim = extent->membase + size;
	init_extent(base, extent);
	__list_append(base, &extent->link, &heap->extents);

	return 0;
}

static int init_main_heap(struct session_heap *m_heap,
			  size_t size)
{
	pthread_mutexattr_t mattr;
	int ret;

	ret = init_heap(&m_heap->heap, m_heap, "main", m_heap + 1, size);
	if (ret)
		return __bt(ret);

	m_heap->cpid = get_thread_pid();

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, mutex_type_attribute);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
	ret = __bt(-__RT(pthread_mutex_init(&m_heap->sysgroup.lock, &mattr)));
	pthread_mutexattr_destroy(&mattr);
	if (ret)
		return ret;

	__hash_init(m_heap, &m_heap->catalog);
	m_heap->sysgroup.thread_count = 0;
	__list_init(m_heap, &m_heap->sysgroup.thread_list);
	m_heap->sysgroup.heap_count = 0;
	__list_init(m_heap, &m_heap->sysgroup.heap_list);

	return 0;
}

static caddr_t get_free_range(struct shared_heap *heap, size_t bsize, int log2size)
{
	caddr_t block, eblock, freepage, lastpage, headpage, freehead = NULL;
	struct shared_extent *extent;
	size_t pnum, pcont, fcont;
	void *base = main_base;

	__list_for_each_entry(base, extent, &heap->extents, link) {
		freepage = __shref_check(base, extent->freelist);
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
				freepage = __shref_check(base, *((memoff_t *)freepage));
				fcont += HOBJ_PAGE_SIZE;
			} while (freepage == lastpage + HOBJ_PAGE_SIZE
				 && fcont < bsize);
			if (fcont >= bsize) {
				/*
				 * Ok, got it. Just update the free
				 * page list, then proceed to the next
				 * step.
				 */
				if (__shoff(base, headpage) == extent->freelist)
					extent->freelist = *((memoff_t *)lastpage);
				else
					*((memoff_t *)freehead) = *((memoff_t *)lastpage);

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
			*((memoff_t *)block) = __shoff(base, block) + bsize;

		*((memoff_t *)eblock) = 0;
	} else
		*((memoff_t *)headpage) = 0;

	pnum = (__shoff(base, headpage) - extent->membase) >> HOBJ_PAGE_SHIFT;

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
	extent->pagemap[pnum].type = log2size ?: page_list;
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

static void *alloc_block(struct shared_heap *heap, size_t size)
{
	struct shared_extent *extent;
	void *base = main_base;
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

		block = __shref_check(base, heap->buckets[ilog].freelist);
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
			__list_for_each_entry(base, extent, &heap->extents, link) {
				if (__shoff(base, block) >= extent->membase &&
				    __shoff(base, block) < extent->memlim)
					goto found;
			}
			assert(0);
		found:
			pnum = (__shoff(base, block) - extent->membase) >> HOBJ_PAGE_SHIFT;
			++extent->pagemap[pnum].bcount;
		}

		heap->buckets[ilog].freelist = *((memoff_t *)block);
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

static int free_block(struct shared_heap *heap, void *block)
{
	int log2size, ret = 0, nblocks, xpage, ilog, pagenr, maxpages;
	caddr_t freepage, lastpage, nextpage, tailpage, freeptr;
	size_t pnum, pcont, boffset, bsize;
	struct shared_extent *extent;
	void *base = main_base;
	memoff_t *tailptr;

	write_lock_nocancel(&heap->lock);

	/*
	 * Find the extent from which the returned block is
	 * originating from.
	 */
	__list_for_each_entry(base, extent, &heap->extents, link) {
		if (__shoff(base, block) >= extent->membase &&
		    __shoff(base, block) < extent->memlim)
			goto found;
	}

	ret = -EFAULT;
	goto out;
found:
	/* Compute the heading page number in the page map. */
	pnum = (__shoff(base, block) - extent->membase) >> HOBJ_PAGE_SHIFT;
	boffset = (__shoff(base, block) -
		   (extent->membase + (pnum << HOBJ_PAGE_SHIFT)));

	switch (extent->pagemap[pnum].type) {
	case page_free:	/* Unallocated page? */
	case page_cont:	/* Not a range heading page? */
		ret = -EINVAL;
		goto out;

	case page_list:
		pagenr = 1;
		maxpages = (extent->memlim - extent->membase) >> HOBJ_PAGE_SHIFT;
		while (pagenr < maxpages &&
		       extent->pagemap[pnum + pagenr].type == page_cont)
			pagenr++;
		bsize = pagenr * HOBJ_PAGE_SIZE;

	free_page_list:
		/* Link all freed pages in a single sub-list. */
		for (freepage = (caddr_t)block,
		     tailpage = (caddr_t)block + bsize - HOBJ_PAGE_SIZE;
		     freepage < tailpage; freepage += HOBJ_PAGE_SIZE)
			*((memoff_t *)freepage) = __shoff(base, freepage) + HOBJ_PAGE_SIZE;

	free_pages:
		/* Mark the released pages as free in the extent's page map. */
		for (pcont = 0; pcont < pagenr; pcont++)
			extent->pagemap[pnum + pcont].type = page_free;
		/*
		 * Return the sub-list to the free page list, keeping
		 * an increasing address order to favor coalescence.
		 */
		for (nextpage = __shref_check(base, extent->freelist), lastpage = NULL;
		     nextpage && nextpage < (caddr_t)block;
		     lastpage = nextpage, nextpage = __shref_check(base, *((memoff_t *)nextpage)))
		  ;	/* Loop */

		*((memoff_t *)tailpage) = __shoff_check(base, nextpage);
		if (lastpage)
			*((memoff_t *)lastpage) = __shoff(base, block);
		else
			extent->freelist = __shoff(base, block);
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
			*((memoff_t *)block) = heap->buckets[ilog].freelist;
			heap->buckets[ilog].freelist = __shoff(base, block);
			++heap->buckets[ilog].fcount;
			break;
		}

		pagenr = bsize >> HOBJ_PAGE_SHIFT;
		if (pagenr > 1)
			/*
			 * The simplest case: we only have a single
			 * block to deal with, which spans multiple
			 * pages. We just need to release it as a list
			 * of pages, without caring about the
			 * consistency of the bucket.
			 */
			goto free_page_list;

		freepage = __shref(base, extent->membase) + (pnum << HOBJ_PAGE_SHIFT);
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
			     freeptr = __shref_check(base, *tailptr), xpage = 1;
		     freeptr && nblocks > 0;
		     freeptr = __shref_check(base, *((memoff_t *)freeptr))) {
			if (freeptr < freepage || freeptr >= nextpage) {
				if (xpage) { /* Limit random writes */
					*tailptr = __shoff(base, freeptr);
					xpage = 0;
				}
				tailptr = (memoff_t *)freeptr;
			} else {
				--nblocks;
				xpage = 1;
			}
		}
		*tailptr = __shoff_check(base, freeptr);
		goto free_pages;
	}

	heap->ubytes -= bsize;
out:
	write_unlock(&heap->lock);

	return __bt(ret);
}

static size_t check_block(struct shared_heap *heap, void *block)
{
	size_t pnum, boffset, bsize, ret = 0;
	struct shared_extent *extent;
	void *base = main_base;
	int ptype;

	read_lock_nocancel(&heap->lock);

	/*
	 * Find the extent the checked block is originating from.
	 */
	__list_for_each_entry(base, extent, &heap->extents, link) {
		if (__shoff(base, block) >= extent->membase &&
		    __shoff(base, block) < extent->memlim)
			goto found;
	}
	goto out;
found:
	/* Compute the heading page number in the page map. */
	pnum = (__shoff(base, block) - extent->membase) >> HOBJ_PAGE_SHIFT;
	ptype = extent->pagemap[pnum].type;
	if (ptype == page_free || ptype == page_cont)
		goto out;

	bsize = (1 << ptype);
	boffset = (__shoff(base, block) -
		   (extent->membase + (pnum << HOBJ_PAGE_SHIFT)));
	if ((boffset & (bsize - 1)) != 0) /* Not a block start? */
		goto out;

	ret = bsize;
out:
	read_unlock(&heap->lock);

	return ret;
}

static int create_main_heap(pid_t *cnode_r)
{
	const char *session = __copperplate_setup_data.session_label;
	gid_t gid =__copperplate_setup_data.session_gid;
	size_t size = __copperplate_setup_data.mem_pool;
	struct heapobj *hobj = &main_pool;
	struct session_heap *m_heap;
	struct stat sbuf;
	memoff_t len;
	int ret, fd;

	/*
	 * A storage page should be obviously larger than an extent
	 * header, but we still make sure of this in debug mode, so
	 * that we can rely on __align_to() for rounding to the
	 * minimum size in production builds, without any further
	 * test (e.g. like size >= sizeof(struct shared_extent)).
	 */
	assert(HOBJ_PAGE_SIZE > sizeof(struct shared_extent));

	*cnode_r = -1;
	size = __align_to(size, HOBJ_PAGE_SIZE);
	if (size > HOBJ_MAXEXTSZ)
		return __bt(-EINVAL);

	if (size < HOBJ_PAGE_SIZE * 2)
		size = HOBJ_PAGE_SIZE * 2;

	len = size + sizeof(*m_heap);
	len += get_pagemap_size(size);

	/*
	 * Bind to (and optionally create) the main session's heap:
	 *
	 * If the heap already exists, check whether the leading
	 * process who created it is still alive, in which case we'll
	 * bind to it, unless the requested size differs.
	 *
	 * Otherwise, create the heap for the new emerging session and
	 * bind to it.
	 */
	snprintf(hobj->name, sizeof(hobj->name), "%s.heap", session);
	snprintf(hobj->fsname, sizeof(hobj->fsname),
		 "/xeno:%s", hobj->name);

	fd = shm_open(hobj->fsname, O_RDWR|O_CREAT, 0660);
	if (fd < 0)
		return __bt(-errno);

	ret = flock(fd, LOCK_EX);
	if (__bterrno(ret))
		goto errno_fail;

	ret = fstat(fd, &sbuf);
	if (__bterrno(ret))
		goto errno_fail;

	if (sbuf.st_size == 0)
		goto init;

	m_heap = __STD(mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0));
	if (m_heap == MAP_FAILED) {
		ret = __bt(-errno);
		goto close_fail;
	}

	if (m_heap->cpid == 0)
		goto reset;

	if (copperplate_probe_tid(m_heap->cpid) == 0) {
		if (m_heap->maplen == len) {
			/* CAUTION: __moff() depends on __main_heap. */
			__main_heap = m_heap;
			__main_sysgroup = &m_heap->sysgroup;
			hobj->pool_ref = __moff(&m_heap->heap);
			goto done;
		}
		*cnode_r = m_heap->cpid;
		munmap(m_heap, len);
		__STD(close(fd));
		return __bt(-EEXIST);
	}
reset:
	munmap(m_heap, len);
	/*
	 * Reset shared memory ownership to revoke permissions from a
	 * former session with more permissive access rules, such as
	 * group-controlled access.
	 */
	ret = fchown(fd, geteuid(), getegid());
	(void)ret;
init:
	ret = ftruncate(fd, 0);  /* Clear all previous contents if any. */
	if (__bterrno(ret))
		goto unlink_fail;

	ret = ftruncate(fd, len);
	if (__bterrno(ret))
		goto unlink_fail;

	/*
	 * If we need to share the heap between members of a group,
	 * give the group RW access to the shared memory file backing
	 * the heap.
	 */
	if (gid != USHRT_MAX) {
		ret = fchown(fd, geteuid(), gid);
		if (__bterrno(ret) < 0)
			goto unlink_fail;
		ret = fchmod(fd, 0660);
		if (__bterrno(ret) < 0)
			goto unlink_fail;
	}

	m_heap = __STD(mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0));
	if (m_heap == MAP_FAILED) {
		ret = __bt(-errno);
		goto unlink_fail;
	}

	m_heap->maplen = len;
	/* CAUTION: init_main_heap() depends on hobj->pool_ref. */
	hobj->pool_ref = __moff(&m_heap->heap);
	ret = __bt(init_main_heap(m_heap, size));
	if (ret) {
		errno = -ret;
		goto unmap_fail;
	}

	/* We need these globals set up before updating a sysgroup. */
	__main_heap = m_heap;
	__main_sysgroup = &m_heap->sysgroup;
	sysgroup_add(heap, &m_heap->heap.memspec);
done:
	flock(fd, LOCK_UN);
	__STD(close(fd));
	hobj->size = m_heap->heap.total;
	__main_catalog = &m_heap->catalog;

	return 0;
unmap_fail:
	munmap(m_heap, len);
unlink_fail:
	ret = -errno;
	shm_unlink(hobj->fsname);
	goto close_fail;
errno_fail:
	ret = __bt(-errno);
close_fail:
	__STD(close(fd));

	return ret;
}

static int bind_main_heap(const char *session)
{
	struct heapobj *hobj = &main_pool;
	struct session_heap *m_heap;
	int ret, fd, cpid;
	struct stat sbuf;
	memoff_t len;

	/* No error tracking, this is for internal users. */

	snprintf(hobj->name, sizeof(hobj->name), "%s.heap", session);
	snprintf(hobj->fsname, sizeof(hobj->fsname),
		 "/xeno:%s", hobj->name);

	fd = shm_open(hobj->fsname, O_RDWR, 0400);
	if (fd < 0)
		return -errno;

	ret = flock(fd, LOCK_EX);
	if (ret)
		goto errno_fail;

	ret = fstat(fd, &sbuf);
	if (ret)
		goto errno_fail;

	len = sbuf.st_size;
	if (len < sizeof(*m_heap)) {
		ret = -EINVAL;
		goto fail;
	}

	m_heap = __STD(mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0));
	if (m_heap == MAP_FAILED)
		goto errno_fail;

	cpid = m_heap->cpid;
	__STD(close(fd));

	if (cpid == 0 || copperplate_probe_tid(cpid)) {
		munmap(m_heap, len);
		return -ENOENT;
	}

	hobj->pool_ref = __moff(&m_heap->heap);
	hobj->size = m_heap->heap.total;
	__main_heap = m_heap;
	__main_catalog = &m_heap->catalog;
	__main_sysgroup = &m_heap->sysgroup;

	return 0;

errno_fail:
	ret = -errno;
fail:
	__STD(close(fd));

	return ret;
}

int pshared_check(void *__heap, void *__addr)
{
	struct shared_heap *heap = __heap;
	struct shared_extent *extent;
	struct session_heap *m_heap;

	/*
	 * Fast check for the main heap: we have a single extent for
	 * this one, so the address shall fall into the file-backed
	 * memory range.
	 */
	if (__moff(heap) == main_pool.pool_ref) {
		m_heap = container_of(heap, struct session_heap, heap);
		return __addr >= (void *)m_heap &&
			__addr < (void *)m_heap + m_heap->maplen;
	}

	/*
	 * Secondary (nested) heap: some refs may fall into the
	 * header, check for this first.
	 */
	if (__addr >= __heap && __addr < __heap + sizeof(*heap))
		return 1;

	/*
	 * This address must be referring to some payload data within
	 * the nested heap, check that it falls into one of the heap
	 * extents.
	 */
	assert(!list_empty(&heap->extents));

	__list_for_each_entry(main_base, extent, &heap->extents, link) {
		if (__shoff(main_base, __addr) >= extent->membase &&
		    __shoff(main_base, __addr) < extent->memlim)
			return 1;
	}

	return 0;
}

int heapobj_init(struct heapobj *hobj, const char *name, size_t size)
{
	const char *session = __copperplate_setup_data.session_label;
	struct shared_heap *heap;
	size_t len;

	size = __align_to(size, HOBJ_PAGE_SIZE);
	if (size > HOBJ_MAXEXTSZ)
		return __bt(-EINVAL);

	if (size < HOBJ_PAGE_SIZE * 2)
		size = HOBJ_PAGE_SIZE * 2;

	len = size + sizeof(*heap);
	len += get_pagemap_size(size);

	/*
	 * Create a heap nested in the main shared heap to hold data
	 * we can share among processes which belong to the same
	 * session.
	 */
	heap = alloc_block(&main_heap.heap, len);
	if (heap == NULL) {
		warning("%s() failed for %Zu bytes, raise --mem-pool-size?",
			__func__);
		return __bt(-ENOMEM);
	}

	if (name)
		snprintf(hobj->name, sizeof(hobj->name), "%s.%s",
			 session, name);
	else
		snprintf(hobj->name, sizeof(hobj->name), "%s.%p",
			 session, hobj);

	init_heap(heap, main_base, hobj->name, heap + 1, size);
	hobj->pool_ref = __moff(heap);
	hobj->size = heap->total;
	sysgroup_add(heap, &heap->memspec);

	return 0;
}

int heapobj_init_array(struct heapobj *hobj, const char *name,
		       size_t size, int elems)
{
	size = align_alloc_size(size);
	return __bt(heapobj_init(hobj, name, size * elems));
}

void heapobj_destroy(struct heapobj *hobj)
{
	struct shared_heap *heap = __mptr(hobj->pool_ref);
	int cpid;

	if (hobj != &main_pool) {
		__RT(pthread_mutex_destroy(&heap->lock));
		sysgroup_remove(heap, &heap->memspec);
		free_block(&main_heap.heap, heap);
		return;
	}

	cpid = main_heap.cpid;
	if (cpid != 0 && cpid != get_thread_pid() &&
	    copperplate_probe_tid(cpid) == 0) {
		munmap(&main_heap, main_heap.maplen);
		return;
	}
	
	__RT(pthread_mutex_destroy(&heap->lock));
	__RT(pthread_mutex_destroy(&main_heap.sysgroup.lock));
	munmap(&main_heap, main_heap.maplen);
	shm_unlink(hobj->fsname);
}

int heapobj_extend(struct heapobj *hobj, size_t size, void *unused)
{
	struct shared_heap *heap = __mptr(hobj->pool_ref);
	struct shared_extent *extent;
	size_t metasz;
	int state;

	if (hobj == &main_pool)	/* Can't extend the main pool. */
		return __bt(-EINVAL);

	size = __align_to(size, HOBJ_PAGE_SIZE);
	metasz = get_pagemap_size(size);
	extent = alloc_block(&main_heap.heap, size + metasz);
	if (extent == NULL)
		return __bt(-ENOMEM);

	extent->membase = __shoff(main_base, extent) + metasz;
	extent->memlim = extent->membase + size;
	init_extent(main_base, extent);
	write_lock_safe(&heap->lock, state);
	__list_append(heap, &extent->link, &heap->extents);
	if (size > heap->maxcont)
		heap->maxcont = size;
	heap->total += size;
	hobj->size += size;
	write_unlock_safe(&heap->lock, state);

	return 0;
}

void *heapobj_alloc(struct heapobj *hobj, size_t size)
{
	return alloc_block(__mptr(hobj->pool_ref), size);
}

void heapobj_free(struct heapobj *hobj, void *ptr)
{
	free_block(__mptr(hobj->pool_ref), ptr);
}

size_t heapobj_validate(struct heapobj *hobj, void *ptr)
{
	return __bt(check_block(__mptr(hobj->pool_ref), ptr));
}

size_t heapobj_inquire(struct heapobj *hobj)
{
	struct shared_heap *heap = __mptr(hobj->pool_ref);
	return heap->ubytes;
}

void *xnmalloc(size_t size)
{
	return alloc_block(&main_heap.heap, size);
}

void xnfree(void *ptr)
{
	free_block(&main_heap.heap, ptr);
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
	pid_t cnode;
	int ret;

	ret = create_main_heap(&cnode);
	if (ret == -EEXIST)
		warning("session %s is still active (pid %d)\n",
			__copperplate_setup_data.session_label, cnode);

	return __bt(ret);
}

int heapobj_bind_session(const char *session)
{
	/* No error tracking, this is for internal users. */
	return bind_main_heap(session);
}

void heapobj_unbind_session(void)
{
	size_t len = main_heap.maplen;

	munmap(&main_heap, len);
}

int heapobj_unlink_session(const char *session)
{
	char *path;
	int ret;

	ret = asprintf(&path, "/xeno:%s.heap", session);
	if (ret < 0)
		return -ENOMEM;
	ret = shm_unlink(path) ? -errno : 0;
	free(path);

	return ret;
}

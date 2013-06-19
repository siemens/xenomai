/*
 * @note Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
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
 *
 * \ingroup heap
 */

#ifndef _COBALT_KERNEL_HEAP_H
#define _COBALT_KERNEL_HEAP_H

/*
 * CONSTRAINTS:
 *
 * Minimum page size is 2 ** XNHEAP_MINLOG2 (must be large enough to
 * hold a pointer).
 *
 * Maximum page size is 2 ** XNHEAP_MAXLOG2.
 *
 * Minimum block size equals the minimum page size.
 *
 * Requested block size smaller than the minimum block size is
 * rounded to the minimum block size.
 *
 * Requested block size larger than 2 times the page size is rounded
 * to the next page boundary and obtained from the free page
 * list. So we need a bucket for each power of two between
 * XNHEAP_MINLOG2 and XNHEAP_MAXLOG2 inclusive, plus one to honor
 * requests ranging from the maximum page size to twice this size.
 */

#ifdef __KERNEL__

#include <linux/mm.h>
#include <cobalt/kernel/queue.h>
#include <cobalt/kernel/list.h>

#define XNHEAP_PAGE_SIZE	512 /* A reasonable value for the xnheap page size */
#define XNHEAP_PAGE_MASK	(~(XNHEAP_PAGE_SIZE-1))
#define XNHEAP_PAGE_ALIGN(addr)	(((addr)+XNHEAP_PAGE_SIZE-1)&XNHEAP_PAGE_MASK)

#define XNHEAP_MINLOG2    3
#define XNHEAP_MAXLOG2    22	/* Must hold pagemap::bcount objects */
#define XNHEAP_MINALLOCSZ (1 << XNHEAP_MINLOG2)
#define XNHEAP_MINALIGNSZ (1 << 4) /* i.e. 16 bytes */
#define XNHEAP_NBUCKETS   (XNHEAP_MAXLOG2 - XNHEAP_MINLOG2 + 2)
#define XNHEAP_MAXEXTSZ   (1 << 31) /* i.e. 2Gb */

#define XNHEAP_PFREE   0
#define XNHEAP_PCONT   1
#define XNHEAP_PLIST   2

#define XNHEAP_GFP_NONCACHED (1 << __GFP_BITS_SHIFT)

struct xnpagemap {
	unsigned int type : 8;	  /* PFREE, PCONT, PLIST or log2 */
	unsigned int bcount : 24; /* Number of active blocks. */
};

struct xnextent {
	/** xnheap->extents */
	struct list_head link;
	/** Base address of the page array */
	caddr_t membase;
	/** Memory limit of page array */
	caddr_t memlim;
	/** Head of the free page list */
	caddr_t freelist;
	/** Beginning of page map */
	struct xnpagemap pagemap[1];
};

struct xnheap {

	xnholder_t link;

	u_long extentsize,
		pagesize,
		pageshift,
		hdrsize,
		npages,		/* Number of pages per extent */
		ubytes,
		maxcont;

	struct list_head extents;
	int nrextents;

	DECLARE_XNLOCK(lock);

	struct xnbucket {
		caddr_t freelist;
		int fcount;
	} buckets[XNHEAP_NBUCKETS];

	xnholder_t *idleq[NR_CPUS];

	/* # of active user-space mappings. */
	unsigned long numaps;
	/* Kernel memory flags (0 if vmalloc()). */
	int kmflags;
	/* Shared heap memory base. */
	void *heapbase;
	/* Callback upon last munmap. */
	void (*release)(struct xnheap *heap);

	/** Link in heapq */
	struct list_head stat_link;

	char label[XNOBJECT_NAME_LEN+16];
};

extern struct xnheap kheap;

#define xnheap_extentsize(heap)		((heap)->extentsize)
#define xnheap_page_size(heap)		((heap)->pagesize)
#define xnheap_page_count(heap)		((heap)->npages)
#define xnheap_usable_mem(heap)		((heap)->maxcont * (heap)->nrextents)
#define xnheap_used_mem(heap)		((heap)->ubytes)
#define xnheap_max_contiguous(heap)	((heap)->maxcont)

static inline size_t xnheap_align(size_t size, size_t al)
{
	/* The alignment value must be a power of 2 */
	return ((size+al-1)&(~(al-1)));
}

static inline size_t xnheap_external_overhead(size_t hsize, size_t psize)
{
	size_t pages = (hsize + psize - 1) / psize;
	return xnheap_align(sizeof(struct xnextent)
			    + pages * sizeof(struct xnpagemap), psize);
}

static inline size_t xnheap_internal_overhead(size_t hsize, size_t psize)
{
	/* o = (h - o) * m / p + e
	   o * p = (h - o) * m + e * p
	   o * (p + m) = h * m + e * p
	   o = (h * m + e *p) / (p + m)
	*/
	return xnheap_align((sizeof(struct xnextent) * psize
			     + sizeof(struct xnpagemap) * hsize)
			    / (psize + sizeof(struct xnpagemap)), psize);
}

#define xnmalloc(size)     xnheap_alloc(&kheap,size)
#define xnfree(ptr)        xnheap_free(&kheap,ptr)
#define xnfreesync()       xnheap_finalize_free(&kheap)

static inline size_t xnheap_rounded_size(size_t hsize, size_t psize)
{
	/*
	 * Account for the minimum heap size (i.e. 2 * page size) plus
	 * overhead so that the actual heap space is large enough to
	 * match the requested size. Using a small page size for large
	 * single-block heaps might reserve a lot of useless page map
	 * memory, but this should never get pathological anyway,
	 * since we only consume 4 bytes per page.
	 */
	if (hsize < 2 * psize)
		hsize = 2 * psize;
	hsize += xnheap_external_overhead(hsize, psize);
	return xnheap_align(hsize, psize);
}

/* Private interface. */

#ifdef __KERNEL__

int xnheap_mount(void);

void xnheap_umount(void);

void xnheap_init_proc(void);

void xnheap_cleanup_proc(void);

int xnheap_init_mapped(struct xnheap *heap,
		       u_long heapsize,
		       int memflags);

void xnheap_destroy_mapped(struct xnheap *heap,
			   void (*release)(struct xnheap *heap),
			   void __user *mapaddr);

#define xnheap_base_memory(heap) \
	((unsigned long)((heap)->heapbase))

#define xnheap_mapped_offset(heap,ptr) \
	(((caddr_t)(ptr)) - (caddr_t)xnheap_base_memory(heap))

#define xnheap_mapped_address(heap,off) \
	((caddr_t)xnheap_base_memory(heap) + (off))

#define xnheap_mapped_p(heap) \
	(xnheap_base_memory(heap) != 0)

#endif /* __KERNEL__ */

/* Public interface. */

int xnheap_init(struct xnheap *heap,
		void *heapaddr,
		u_long heapsize,
		u_long pagesize);

void xnheap_set_label(struct xnheap *heap, const char *name, ...);

void xnheap_destroy(struct xnheap *heap,
		    void (*flushfn)(struct xnheap *heap,
				    void *extaddr,
				    u_long extsize,
				    void *cookie),
		    void *cookie);

int xnheap_extend(struct xnheap *heap,
		  void *extaddr,
		  u_long extsize);

void *xnheap_alloc(struct xnheap *heap,
		   u_long size);

int xnheap_test_and_free(struct xnheap *heap,
			 void *block,
			 int (*ckfn)(void *block));

int xnheap_free(struct xnheap *heap,
		void *block);

void xnheap_schedule_free(struct xnheap *heap,
			  void *block,
			  xnholder_t *link);

void xnheap_finalize_free_inner(struct xnheap *heap,
				int cpu);

static inline void xnheap_finalize_free(struct xnheap *heap)
{
	int cpu = ipipe_processor_id();

	XENO_ASSERT(NUCLEUS,
		    spltest() != 0,
		    xnpod_fatal("%s called in unsafe context", __FUNCTION__));

	if (heap->idleq[cpu])
		xnheap_finalize_free_inner(heap, cpu);
}

int xnheap_check_block(struct xnheap *heap,
		       void *block);

int xnheap_remap_vm_page(struct vm_area_struct *vma,
			 unsigned long from, unsigned long to);

int xnheap_remap_io_page_range(struct file *filp,
			       struct vm_area_struct *vma,
			       unsigned long from, phys_addr_t to,
			       unsigned long size, pgprot_t prot);

int xnheap_remap_kmem_page_range(struct vm_area_struct *vma,
				 unsigned long from, unsigned long to,
				 unsigned long size, pgprot_t prot);

#endif /* __KERNEL__ */

#define XNHEAP_DEV_NAME  "/dev/rtheap"
#define XNHEAP_DEV_MINOR 254

/* Possible arguments to the sys_heap_info syscall */
#define XNHEAP_PROC_PRIVATE_HEAP 0
#define XNHEAP_PROC_SHARED_HEAP  1
#define XNHEAP_SYS_HEAP          2

struct xnheap_desc {
	unsigned long handle;
	unsigned int size;
	unsigned long area;
	unsigned long used;
};

#endif /* !_COBALT_KERNEL_HEAP_H */

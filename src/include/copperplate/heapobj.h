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

#ifndef _COPPERPLATE_HEAPOBJ_H
#define _COPPERPLATE_HEAPOBJ_H

#include <sys/types.h>
#include <assert.h>
#include <xeno_config.h>
#include <copperplate/reference.h>

#ifdef CONFIG_XENO_PSHARED

struct heapobj;
struct hash_table;

struct heapobj_ops {
	void (*destroy)(struct heapobj *hobj);
	int (*extend)(struct heapobj *hobj, size_t size, void *mem);
	void *(*alloc)(struct heapobj *hobj, size_t size);
	void *(*realloc)(struct heapobj *hobj, void *ptr, size_t size);
	void (*free)(struct heapobj *hobj, void *ptr);
	size_t (*inquire)(struct heapobj *hobj, void *ptr);
};

/*
 * The heap control block is always heading the shared memory segment,
 * so that any process can access this information right after the
 * segment is mmapped. This also ensures that offset 0 will never
 * refer to a valid page or block.
 */
extern void *__pshared_heap;
#define main_heap		(*((struct heap *)__pshared_heap))

extern struct hash_table *__pshared_catalog;
#define main_catalog		(*((struct hash_table *)__pshared_catalog))

#else /* !CONFIG_XENO_PSHARED */

/*
 * Whether an object is laid in some shared heap. Never if pshared
 * mode is disabled.
 */
static inline int pshared_check(void *heap, void *addr)
{
	return 0;
}

#endif	/* !CONFIG_XENO_PSHARED */

struct heapobj {
	void *pool;
	size_t size;
	char name[64];
#ifdef CONFIG_XENO_PSHARED
	struct heapobj_ops *ops;
	char fsname[64];
	int fd;
#endif
};

static inline void *mainheap_ptr(off_t off)
{
	return off ? (void *)__memptr(__pshared_heap, off) : NULL;
}

static inline off_t mainheap_off(void *addr)
{
	return addr ? (off_t)__memoff(__pshared_heap, addr) : 0;
}

/*
 * Handles of shared heap-based pointers have bit #0 set. Other values
 * are not translated, and the return value is the original handle
 * cast to a pointer. A null handle is always returned unchanged.
 */
#define mainheap_deref(handle, type)					\
	({								\
		type *ptr;						\
		assert(__builtin_types_compatible_p(typeof(handle), intptr_t)); \
		ptr = (handle & 1) ? (type *)mainheap_ptr(handle & ~1UL) : (type *)handle; \
		ptr;							\
	})

/*
 * ptr shall point to a block of memory allocated within the main heap
 * if non-null; such address is always 8-byte aligned. Handles of
 * shared heap pointers are returned with bit #0 set, which serves as
 * a special tag detected in mainhead_deref(). A null pointer is
 * always translated as a null handle.
 */
#define mainheap_ref(ptr, type)						\
	({								\
		type handle;						\
		assert(__builtin_types_compatible_p(typeof(type), intptr_t)); \
		assert(ptr == NULL || __memchk(__pshared_heap, ptr));	\
		handle = (type)mainheap_off(ptr);			\
		handle|1;						\
	})

#ifdef __cplusplus
extern "C" {
#endif

int heapobj_pkg_init_private(void);

int heapobj_init_private(struct heapobj *hobj, const char *name,
			 size_t size, void *mem);

int heapobj_init_array_private(struct heapobj *hobj, const char *name,
			       size_t size, int elems);

void *pvmalloc(size_t size);

void *pvrealloc(void *ptr, size_t size);

void pvfree(void *ptr);

char *pvstrdup(const char *ptr);

#ifdef CONFIG_XENO_PSHARED

int heapobj_pkg_init_shared(void);

int heapobj_init(struct heapobj *hobj, const char *name,
		 size_t size, void *mem);

int heapobj_init_array(struct heapobj *hobj, const char *name,
		       size_t size, int elems);

static inline void heapobj_destroy(struct heapobj *hobj)
{
	hobj->ops->destroy(hobj);
}

static inline int heapobj_extend(struct heapobj *hobj,
				 size_t size, void *mem)
{
	return hobj->ops->extend(hobj, size, mem);
}

static inline void *heapobj_alloc(struct heapobj *hobj, size_t size)
{
	return hobj->ops->alloc(hobj, size);
}

static inline void *heapobj_realloc(struct heapobj *hobj,
				    void *ptr, size_t size)
{
	return hobj->ops->realloc(hobj, ptr, size);
}

static inline void heapobj_free(struct heapobj *hobj, void *ptr)
{
	hobj->ops->free(hobj, ptr);
}

static inline size_t heapobj_inquire(struct heapobj *hobj, void *ptr)
{
	return hobj->ops->inquire(hobj, ptr);
}

void *xnmalloc(size_t size);

void *xnrealloc(void *ptr, size_t size);

void xnfree(void *ptr);

char *xnstrdup(const char *ptr);

#else /* !CONFIG_XENO_PSHARED */

void mem_destroy(struct heapobj *hobj);

int mem_extend(struct heapobj *hobj, size_t size, void *mem);

void *mem_alloc(struct heapobj *hobj, size_t size);

void *mem_realloc(struct heapobj *hobj, void *ptr, size_t size);

void mem_free(struct heapobj *hobj, void *ptr);

size_t mem_inquire(struct heapobj *hobj, void *ptr);

static inline int heapobj_pkg_init_shared(void)
{
	return 0;
}

static inline int heapobj_init(struct heapobj *hobj, const char *name,
			       size_t size, void *mem)
{
	return heapobj_init_private(hobj, name, size, mem);
}

static inline int heapobj_init_array(struct heapobj *hobj, const char *name,
				     size_t size, int elems)
{
	return heapobj_init_array_private(hobj, name, size, elems);
}

static inline void heapobj_destroy(struct heapobj *hobj)
{
	mem_destroy(hobj);
}

static inline int heapobj_extend(struct heapobj *hobj,
				 size_t size, void *mem)
{
	return mem_extend(hobj, size, mem);
}

static inline void *heapobj_alloc(struct heapobj *hobj, size_t size)
{
	return mem_alloc(hobj, size);
}

static inline void *heapobj_realloc(struct heapobj *hobj,
				    void *ptr, size_t size)
{
	return mem_realloc(hobj, ptr, size);
}

static inline void heapobj_free(struct heapobj *hobj, void *ptr)
{
	mem_free(hobj, ptr);
}

static inline size_t heapobj_inquire(struct heapobj *hobj, void *ptr)
{
	return mem_inquire(hobj, ptr);
}

static inline void *xnmalloc(size_t size)
{
	return pvmalloc(size);
}

static inline void *xnrealloc(void *ptr, size_t size)
{
	return pvrealloc(ptr, size);
}

static inline void xnfree(void *ptr)
{
	return pvfree(ptr);
}

static inline char *xnstrdup(const char *ptr)
{
	return pvstrdup(ptr);
}

#endif /* !CONFIG_XENO_PSHARED */

#ifdef __cplusplus
}
#endif

#endif /* _COPPERPLATE_HEAPOBJ_H */

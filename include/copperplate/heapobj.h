/*
 * Copyright (C) 2008-2011 Philippe Gerum <rpm@xenomai.org>.
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
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <xeno_config.h>
#include <copperplate/reference.h>
#include <copperplate/wrappers.h>
#include <copperplate/debug.h>

struct heapobj;

#ifdef CONFIG_XENO_PSHARED

struct heapobj {
	void *pool;
	size_t size;
	char name[64];
	char fsname[64];
	int fd;
	int flags;
};

#else /* !CONFIG_XENO_PSHARED */

struct heapobj {
	void *pool;
	size_t size;
	char name[64];
};

#endif /* !CONFIG_XENO_PSHARED */

#ifdef __cplusplus
extern "C" {
#endif

int heapobj_pkg_init_private(void);

int heapobj_init_private(struct heapobj *hobj, const char *name,
			 size_t size, void *mem);

int heapobj_init_array_private(struct heapobj *hobj, const char *name,
			       size_t size, int elems);
#ifdef __cplusplus
}
#endif

#ifdef CONFIG_XENO_TLSF

size_t get_used_size(void *pool);
void destroy_memory_pool(void *pool);
size_t add_new_area(void *pool, size_t size, void *mem);
void *malloc_ex(size_t size, void *pool);
void free_ex(void *pool, void *ptr);
void *tlsf_malloc(size_t size);
void tlsf_free(void *ptr);
size_t malloc_usable_size_ex(void *ptr, void *pool);

static inline
void pvheapobj_destroy(struct heapobj *hobj)
{
	destroy_memory_pool(hobj->pool);
}

static inline
int pvheapobj_extend(struct heapobj *hobj, size_t size, void *mem)
{
	hobj->size = add_new_area(hobj->pool, size, mem);
	if (hobj->size == (size_t)-1)
		return __bt(-EINVAL);

	return 0;
}

static inline
void *pvheapobj_alloc(struct heapobj *hobj, size_t size)
{
	return malloc_ex(size, hobj->pool);
}

static inline
void pvheapobj_free(struct heapobj *hobj, void *ptr)
{
	free_ex(ptr, hobj->pool);
}

static inline
size_t pvheapobj_validate(struct heapobj *hobj, void *ptr)
{
	return malloc_usable_size_ex(ptr, hobj->pool);
}

static inline
size_t pvheapobj_inquire(struct heapobj *hobj)
{
	return get_used_size(hobj->pool);
}

static inline void *pvmalloc(size_t size)
{
	return tlsf_malloc(size);
}

static inline void pvfree(void *ptr)
{
	tlsf_free(ptr);
}

static inline char *pvstrdup(const char *ptr)
{
	char *str;

	str = pvmalloc(strlen(ptr) + 1);
	if (str == NULL)
		return NULL;

	return strcpy(str, ptr);
}

#else /* !CONFIG_XENO_TLSF, i.e. malloc */

#include <malloc.h>

static inline
void pvheapobj_destroy(struct heapobj *hobj)
{
}

static inline
int pvheapobj_extend(struct heapobj *hobj, size_t size, void *mem)
{
	return 0;
}

static inline
void *pvheapobj_alloc(struct heapobj *hobj, size_t size)
{
	/*
	 * XXX: We don't want debug _nrt assertions to trigger when
	 * running over Cobalt if the user picked this allocator, so
	 * we make sure to call the glibc directly, not the Cobalt
	 * wrappers.
	 */
	return __STD(malloc(size));
}

static inline
void pvheapobj_free(struct heapobj *hobj, void *ptr)
{
	__STD(free(ptr));
}

static inline
size_t pvheapobj_validate(struct heapobj *hobj, void *ptr)
{
	/*
	 * We will likely get hard validation here, i.e. crash or
	 * abort if the pointer is wrong. TLSF is a bit smarter, and
	 * pshared definitely does the right thing.
	 */
	return malloc_usable_size(ptr);
}

static inline
size_t pvheapobj_inquire(struct heapobj *hobj)
{
	struct mallinfo m = mallinfo();
	return m.uordblks;
}

static inline void *pvmalloc(size_t size)
{
	return __STD(malloc(size));
}

static inline void pvfree(void *ptr)
{
	__STD(free(ptr));
}

static inline char *pvstrdup(const char *ptr)
{
	return strdup(ptr);
}

#endif /* !CONFIG_XENO_TLSF */

#ifdef CONFIG_XENO_PSHARED

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

static inline void *mainheap_ptr(memoff_t off)
{
	return off ? (void *)__memptr(__pshared_heap, off) : NULL;
}

static inline memoff_t mainheap_off(void *addr)
{
	return addr ? (memoff_t)__memoff(__pshared_heap, addr) : 0;
}

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
		assert(__builtin_types_compatible_p(typeof(type), unsigned long) || \
		       __builtin_types_compatible_p(typeof(type), uintptr_t)); \
		assert(ptr == NULL || __memchk(__pshared_heap, ptr));	\
		handle = (type)mainheap_off(ptr);			\
		handle|1;						\
	})
/*
 * Handles of shared heap-based pointers have bit #0 set. Other values
 * are not translated, and the return value is the original handle
 * cast to a pointer. A null handle is always returned unchanged.
 */
#define mainheap_deref(handle, type)					\
	({								\
		type *ptr;						\
		assert(__builtin_types_compatible_p(typeof(handle), unsigned long) || \
		       __builtin_types_compatible_p(typeof(handle), uintptr_t)); \
		ptr = (handle & 1) ? (type *)mainheap_ptr(handle & ~1UL) : (type *)handle; \
		ptr;							\
	})

int heapobj_pkg_init_shared(void);

int heapobj_init(struct heapobj *hobj, const char *name,
		 size_t size, void *mem);

int heapobj_init_array(struct heapobj *hobj, const char *name,
		       size_t size, int elems);

int heapobj_init_shareable(struct heapobj *hobj, const char *name,
			   size_t size);

int heapobj_init_array_shareable(struct heapobj *hobj, const char *name,
				 size_t size, int elems);

void heapobj_destroy(struct heapobj *hobj);

int heapobj_extend(struct heapobj *hobj,
		   size_t size, void *mem);

void *heapobj_alloc(struct heapobj *hobj,
		    size_t size);

void heapobj_free(struct heapobj *hobj,
		  void *ptr);

size_t heapobj_validate(struct heapobj *hobj,
			void *ptr);

size_t heapobj_inquire(struct heapobj *hobj);

void *xnmalloc(size_t size);

void xnfree(void *ptr);

char *xnstrdup(const char *ptr);

#else /* !CONFIG_XENO_PSHARED */

/*
 * Whether an object is laid in some shared heap. Never if pshared
 * mode is disabled.
 */
static inline int pshared_check(void *heap, void *addr)
{
	return 0;
}

#define mainheap_ref(ptr, type)						\
	({								\
		type handle;						\
		assert(__builtin_types_compatible_p(typeof(type), unsigned long) || \
		       __builtin_types_compatible_p(typeof(type), uintptr_t)); \
		assert(ptr == NULL || __memchk(__pshared_heap, ptr));	\
		handle = (type)ptr;					\
		handle;							\
	})
#define mainheap_deref(handle, type)					\
	({								\
		type *ptr;						\
		assert(__builtin_types_compatible_p(typeof(handle), unsigned long) || \
		       __builtin_types_compatible_p(typeof(handle), uintptr_t)); \
		ptr = (type *)handle;					\
		ptr;							\
	})

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

static inline int heapobj_init_shareable(struct heapobj *hobj,
					 const char *name, size_t size)
{
	return heapobj_init(hobj, name, size, NULL);
}

static inline int heapobj_init_array_shareable(struct heapobj *hobj,
					       const char *name,
					       size_t size, int elems)
{
	return heapobj_init_array(hobj, name, size, elems);
}

static inline void heapobj_destroy(struct heapobj *hobj)
{
	pvheapobj_destroy(hobj);
}

static inline int heapobj_extend(struct heapobj *hobj,
				 size_t size, void *mem)
{
	return pvheapobj_extend(hobj, size, mem);
}

static inline void *heapobj_alloc(struct heapobj *hobj,
				  size_t size)
{
	return pvheapobj_alloc(hobj, size);
}

static inline void heapobj_free(struct heapobj *hobj,
				void *ptr)
{
	return pvheapobj_free(hobj, ptr);
}

static inline size_t heapobj_validate(struct heapobj *hobj,
				      void *ptr)
{
	return pvheapobj_validate(hobj, ptr);
}

static inline size_t heapobj_inquire(struct heapobj *hobj)
{
	return pvheapobj_inquire(hobj);
}

static inline void *xnmalloc(size_t size)
{
	return pvmalloc(size);
}

static inline void xnfree(void *ptr)
{
	return pvfree(ptr);
}

static inline char *xnstrdup(const char *ptr)
{
	return pvstrdup(ptr);
}

#endif	/* !CONFIG_XENO_PSHARED */

static inline const char *heapobj_name(struct heapobj *hobj)
{
	return hobj->name;
}

static inline size_t heapobj_size(struct heapobj *hobj)
{
	return hobj->size;
}

#endif /* _COPPERPLATE_HEAPOBJ_H */

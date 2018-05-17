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
#ifndef _COPPERPLATE_INTERNAL_H
#define _COPPERPLATE_INTERNAL_H

#include <sys/types.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <xeno_config.h>
#include <boilerplate/list.h>
#include <boilerplate/ancillaries.h>
#include <boilerplate/limits.h>
#include <boilerplate/sched.h>
#include <boilerplate/setup.h>
#include <copperplate/heapobj.h>
#include <copperplate/tunables.h>

#ifdef CONFIG_XENO_PSHARED

#include <boilerplate/shavl.h>

#define SHEAPMEM_PAGE_SHIFT	9 /* 2^9 => 512 bytes */
#define SHEAPMEM_PAGE_SIZE	(1UL << SHEAPMEM_PAGE_SHIFT)
#define SHEAPMEM_PAGE_MASK	(~(SHEAPMEM_PAGE_SIZE - 1))
#define SHEAPMEM_MIN_LOG2	4 /* 16 bytes */
/*
 * Use bucketed memory for sizes between 2^SHEAPMEM_MIN_LOG2 and
 * 2^(SHEAPMEM_PAGE_SHIFT-1).
 */
#define SHEAPMEM_MAX		(SHEAPMEM_PAGE_SHIFT - SHEAPMEM_MIN_LOG2)
#define SHEAPMEM_MIN_ALIGN	(1U << SHEAPMEM_MIN_LOG2)
/* Max size of an extent (4Gb - SHEAPMEM_PAGE_SIZE). */
#define SHEAPMEM_MAX_EXTSZ	(4294967295U - SHEAPMEM_PAGE_SIZE + 1)
/* Bits we need for encoding a page # */
#define SHEAPMEM_PGENT_BITS      (32 - SHEAPMEM_PAGE_SHIFT)

/* Each page is represented by a page map entry. */
#define SHEAPMEM_PGMAP_BYTES	sizeof(struct sheapmem_pgentry)

struct sheapmem_pgentry {
	/* Linkage in bucket list. */
	unsigned int prev : SHEAPMEM_PGENT_BITS;
	unsigned int next : SHEAPMEM_PGENT_BITS;
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
 * a range of free pages. sheapmem_range.size is nrpages *
 * SHEAPMEM_PAGE_SIZE. Ranges are indexed by address and size in AVL
 * trees.
 */
struct sheapmem_range {
	struct shavlh addr_node;
	struct shavlh size_node;
	size_t size;
};

struct sheapmem_extent {
	struct holder next;
	memoff_t membase;	/* Base offset of page array */
	memoff_t memlim;	/* Offset limit of page array */
	struct shavl addr_tree;
	struct shavl size_tree;
	struct sheapmem_pgentry pagemap[0]; /* Start of page entries[] */
};

#define __SHEAPMEM_MAP_SIZE(__nrpages)					\
	((__nrpages) * SHEAPMEM_PGMAP_BYTES)

#define __SHEAPMEM_ARENA_SIZE(__size)					\
	(__size +							\
	 __align_to(sizeof(struct sheapmem_extent) +			\
		    __SHEAPMEM_MAP_SIZE((__size) >> SHEAPMEM_PAGE_SHIFT),	\
		    SHEAPMEM_MIN_ALIGN))

/*
 * Calculate the minimal size of the memory arena needed to contain a
 * heap of __user_size bytes, including our meta data for managing it.
 * Usable at build time if __user_size is constant.
 */
#define SHEAPMEM_ARENA_SIZE(__user_size)					\
	__SHEAPMEM_ARENA_SIZE(__align_to(__user_size, SHEAPMEM_PAGE_SIZE))

/*
 * The struct below has to live in shared memory; no direct reference
 * to process local memory in there.
 */
struct shared_heap_memory {
	char name[XNOBJECT_NAME_LEN];
	pthread_mutex_t lock;
	struct listobj extents;
	size_t arena_size;
	size_t usable_size;
	size_t used_size;
	/* Heads of page lists for log2-sized blocks. */
	uint32_t buckets[SHEAPMEM_MAX];
	struct sysgroup_memspec memspec;
};

ssize_t sheapmem_check(struct shared_heap_memory *heap, void *block);

#endif /* CONFIG_XENO_PSHARED */

#ifdef CONFIG_XENO_REGISTRY
#define DEFAULT_REGISTRY_ROOT		CONFIG_XENO_REGISTRY_ROOT
#else
#define DEFAULT_REGISTRY_ROOT		NULL
#endif

struct corethread_attributes {
	size_t stacksize;
	int detachstate;
	int policy;
	struct sched_param_ex param_ex;
	int (*prologue)(void *arg);
	void *(*run)(void *arg);
	void *arg;
	struct {
		int status;
		sem_t warm;
		sem_t *released;
	} __reserved;
};

#ifdef __cplusplus
extern "C" {
#endif

void copperplate_set_current_name(const char *name);

int copperplate_get_current_name(char *name, size_t maxlen);

int copperplate_kill_tid(pid_t tid, int sig);

int copperplate_probe_tid(pid_t tid);
  
int copperplate_create_thread(struct corethread_attributes *cta,
			      pthread_t *ptid);

int copperplate_renice_local_thread(pthread_t ptid, int policy,
				    const struct sched_param_ex *param_ex);

void copperplate_bootstrap_internal(const char *arg0,
				    char *mountpt, int regflags);

#ifdef __cplusplus
}
#endif

#endif /* _COPPERPLATE_INTERNAL_H */

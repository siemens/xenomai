/*
 * Copyright (C) 2018 Philippe Gerum <rpm@xenomai.org>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef SMOKEY_MEMCHECK_H
#define SMOKEY_MEMCHECK_H

#include <sys/types.h>
#include <boilerplate/ancillaries.h>
#include <smokey/smokey.h>

/* Must match RTTST_HEAPCHECK_* flags in uapi/testing.h */
#define MEMCHECK_ZEROOVRD   1
#define MEMCHECK_SHUFFLE    2
#define MEMCHECK_PATTERN    4
#define MEMCHECK_HOT        8
#define MEMCHECK_ALL_FLAGS  0xf

struct memcheck_stat {
	size_t heap_size;
	size_t user_size;
	size_t block_size;
	size_t maximum_free;
	size_t largest_free;
	int nrblocks;
	long alloc_avg_ns;
	long alloc_max_ns;
	long free_avg_ns;
	long free_max_ns;
	int flags;
	struct memcheck_stat *next;
};

struct memcheck_descriptor {
	const char *name;
	int (*init)(void *heap, void *mem, size_t heap_size);
	void (*destroy)(void *heap);
	void *(*alloc)(void *heap, size_t size);
	int (*free)(void *heap, void *block);
	size_t (*get_used_size)(void *heap);
	size_t (*get_usable_size)(void *heap);
	size_t (*get_arena_size)(size_t heap_size);
	size_t seq_min_heap_size;
	size_t seq_max_heap_size;
	int random_rounds;
	size_t pattern_heap_size;
	int pattern_rounds;
	void *heap;
	int valid_flags;
	int (*test_seq)(struct memcheck_descriptor *md,
			size_t heap_size, size_t block_size, int flags);
};

#define HEAP_INIT_T(__p)    ((int (*)(void *heap, void *mem, size_t size))(__p))
#define HEAP_DESTROY_T(__p) ((void (*)(void *heap))(__p))
#define HEAP_ALLOC_T(__p)   ((void *(*)(void *heap, size_t size))(__p))
#define HEAP_FREE_T(__p)    ((int (*)(void *heap, void *block))(__p))
#define HEAP_USED_T(__p)    ((size_t (*)(void *heap))(__p))
#define HEAP_USABLE_T(__p)  ((size_t (*)(void *heap))(__p))

#define MEMCHECK_ARGS					\
	SMOKEY_ARGLIST(					\
		SMOKEY_SIZE(seq_heap_size),		\
		SMOKEY_SIZE(pattern_heap_size),		\
		SMOKEY_INT(random_alloc_rounds),	\
		SMOKEY_INT(pattern_check_rounds),	\
		SMOKEY_INT(max_results),		\
	)
  
#define MEMCHECK_HELP_STRINGS						\
	"\tseq_heap_size=<size[K|M|G]>\tmax. heap size for sequential alloc tests\n" \
	"\tpattern_heap_size=<size[K|M|G]>\tmax. heap size for pattern check test\n" \
	"\trandom_alloc_rounds=<N>\t\t# of rounds of random-size allocations\n" \
	"\tpattern_check_rounds=<N>\t# of rounds of pattern check tests\n" \
	"\tmax_results=<N>\t# of result lines (worst-case first, -1=all)\n" \
	"\tSet --verbose=2 for detailed runtime statistics.\n"

void memcheck_log_stat(struct memcheck_stat *st);

int memcheck_run(struct memcheck_descriptor *md,
		 struct smokey_test *t,
		 int argc, char *const argv[]);

#endif /* SMOKEY_MEMCHECK_H */

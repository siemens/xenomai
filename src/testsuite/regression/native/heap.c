#include <stdlib.h>
#include <stdio.h>

#include <sys/mman.h>

#include <native/task.h>
#include <native/heap.h>

#include "check.h"

#define HEAP_NAME "heap"
#define HEAP_SZ 16384

int main(void)
{
	RT_TASK task;
	RT_HEAP heap;
	unsigned i;
	void *mem;

	mlockall(MCL_CURRENT | MCL_FUTURE);

	fprintf(stderr, "Checking native skin shared heaps\n");

	check_native(rt_task_shadow(&task, "task", 1, 0));

	check_native(rt_heap_create(&heap, HEAP_NAME,
				    HEAP_SZ, H_PRIO | H_SHARED));
	check_native(rt_heap_alloc(&heap, HEAP_SZ, TM_INFINITE, &mem));
	memset(mem, 0xA5, HEAP_SZ);

	for (i = 0; i < HEAP_SZ; i++)
		if (((unsigned char *)mem)[i] != 0xA5) {
			fprintf(stderr, "Test failed at byte %u\n", i);
			rt_heap_delete(&heap);
			exit(EXIT_FAILURE);
		}

	check_native(rt_heap_free(&heap, mem));
	check_native(rt_heap_delete(&heap));

	fprintf(stderr, "native skin shared heaps: success\n");
	return EXIT_SUCCESS;
}

#ifndef XENO_SEM_HEAP_H
#define XENO_SEM_HEAP_H

#include <cobalt/kernel/heap.h>

void cobalt_init_sem_heaps(void);

void *cobalt_map_heap(struct xnheap_desc *hd);

#endif /* XENO_SEM_HEAP_H */

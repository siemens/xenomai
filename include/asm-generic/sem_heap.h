#ifndef XENO_SEM_HEAP_H
#define XENO_SEM_HEAP_H

#include <nucleus/heap.h>

void xeno_init_sem_heaps(void);

void *xeno_map_heap(struct xnheap_desc *hd);

#endif /* XENO_SEM_HEAP_H */

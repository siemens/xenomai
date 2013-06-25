#ifndef _LIB_COBALT_SEM_HEAP_H
#define _LIB_COBALT_SEM_HEAP_H

struct xnheap_desc;

void cobalt_init_sem_heaps(void);

void *cobalt_map_heap(struct xnheap_desc *hd);

struct xnvdso;

extern struct xnvdso *vdso;

#endif /* _LIB_COBALT_SEM_HEAP_H */

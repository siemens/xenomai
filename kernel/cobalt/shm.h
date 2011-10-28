#ifndef MMAN_H
#define MMAN_H

#include <nucleus/registry.h>     /* For associative lists. */

typedef struct {
    void *kaddr;
    unsigned long len;
    cobalt_assoc_t assoc;

#define assoc2umap(laddr) \
    ((cobalt_umap_t *)((unsigned long) (laddr) - offsetof(cobalt_umap_t, assoc)))
} cobalt_umap_t;

int cobalt_xnheap_get(xnheap_t **pheap, void *addr);

void cobalt_shm_ufds_cleanup(cobalt_queues_t *q);

void cobalt_shm_umaps_cleanup(cobalt_queues_t *q);

int cobalt_shm_pkg_init(void);

void cobalt_shm_pkg_cleanup(void);

#endif /* MMAN_H */

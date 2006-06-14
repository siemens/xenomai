#ifndef MMAN_H
#define MMAN_H

#include <posix/registry.h>     /* For associative lists. */

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)

typedef struct {
    void *kaddr;
    unsigned long len;
    pse51_assoc_t assoc;

#define assoc2umap(laddr) \
    ((pse51_umap_t *)((unsigned long) (laddr) - offsetof(pse51_umap_t, assoc)))
} pse51_umap_t;

typedef struct {
    unsigned long kfd;
    pse51_assoc_t assoc;
    
#define assoc2ufd(laddr) \
    ((pse51_ufd_t *)((unsigned long) (laddr) - offsetof(pse51_ufd_t, assoc)))
} pse51_ufd_t;

extern pse51_assocq_t pse51_umaps; /* List of user-space mappings. */
extern pse51_assocq_t pse51_ufds; /* List of user-space descriptors. */

int pse51_xnheap_get(xnheap_t **pheap, void *addr);
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */    

int pse51_shm_pkg_init(void);

void pse51_shm_pkg_cleanup(void);

#endif /* MMAN_H */

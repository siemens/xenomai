#ifndef MMAN_H
#define MMAN_H

#include <posix/registry.h>     /* For associative lists. */

extern pse51_assocq_t pse51_umaps; /* List of user-space mappings. */
extern pse51_assocq_t pse51_ufds; /* List of user-space descriptors. */

int pse51_xnheap_get(xnheap_t **pheap, void *addr);

int pse51_shm_pkg_init(void);

void pse51_shm_pkg_cleanup(void);

#endif /* MMAN_H */

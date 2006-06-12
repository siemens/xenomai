#ifndef MMAN_H
#define MMAN_H

#include <nucleus/queue.h>

struct mm_struct;

typedef xnqueue_t pse51_assocq_t;

extern pse51_assocq_t pse51_umaps; /* List of user-space mappings. */
extern pse51_assocq_t pse51_ufds; /* List of user-space descriptors. */

#define pse51_assocq_init(q) (initq(q))

void pse51_assocq_destroy(pse51_assocq_t *q, void (*destroy)(u_long kobj));

int pse51_assoc_create(pse51_assocq_t *q,
                       u_long kobj,
                       struct mm_struct *mm,
                       u_long uobj);

int pse51_assoc_lookup(pse51_assocq_t *q,
                       u_long *kobj,
                       struct mm_struct *mm,
                       u_long uobj);

int pse51_assoc_remove(pse51_assocq_t *q,
                       u_long *kobj,
                       struct mm_struct *mm,
                       u_long uobj);

int pse51_xnheap_get(xnheap_t **pheap, void *addr);

int pse51_shm_pkg_init(void);

void pse51_shm_pkg_cleanup(void);

#endif /* MMAN_H */

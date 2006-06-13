#ifndef POSIX_MQ_H
#define POSIX_MQ_H

#include <posix/registry.h>     /* For associative lists. */

#define PSE51_MQ_FSTORE_LIMIT 64

extern pse51_assocq_t pse51_uqds; /* List of user-space queues descriptors. */

int pse51_mq_pkg_init(void);

void pse51_mq_pkg_cleanup(void);

#endif /* POSIX_MQ_H */

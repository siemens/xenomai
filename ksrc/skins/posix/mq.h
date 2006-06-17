#ifndef POSIX_MQ_H
#define POSIX_MQ_H

#include <posix/registry.h>     /* For associative lists. */

#define PSE51_MQ_FSTORE_LIMIT 64

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)

void pse51_mq_uqds_cleanup(pse51_queues_t *q);

#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */    

int pse51_mq_pkg_init(void);

void pse51_mq_pkg_cleanup(void);

#endif /* POSIX_MQ_H */

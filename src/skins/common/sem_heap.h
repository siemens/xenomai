#ifndef XENO_SEM_HEAP_H
#define XENO_SEM_HEAP_H

#include <xeno_config.h>

#ifdef CONFIG_XENO_FASTSYNCH
void xeno_init_sem_heaps(void);
#else /* !CONFIG_XENO_FASTSYNCH */
#define xeno_init_sem_heaps()
#endif /* !CONFIG_XENO_FASTSYNCH */

#endif /* XENO_SEM_HEAP_H */

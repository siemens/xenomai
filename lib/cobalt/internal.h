#ifndef XENO_COBALT_INTERNAL_H
#define XENO_COBALT_INTERNAL_H

void __cobalt_thread_harden(void);

int __cobalt_thread_stat(pthread_t tid,
			 struct cobalt_threadstat *stat);

#endif /* XENO_COBALT_INTERNAL_H */

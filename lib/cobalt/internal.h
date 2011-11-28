#ifndef XENO_COBALT_INTERNAL_H
#define XENO_COBALT_INTERNAL_H

void __cobalt_thread_harden(void);

int __cobalt_thread_stat(pthread_t tid,
			 struct cobalt_threadstat *stat);

int __cobalt_event_wait(pthread_cond_t *cond,
			pthread_mutex_t *mutex);

int __cobalt_event_timedwait(pthread_cond_t *cond,
			     pthread_mutex_t *mutex,
			     const struct timespec *abstime);

int __cobalt_event_signal(pthread_cond_t *cond);

int __cobalt_event_broadcast(pthread_cond_t *cond);

#endif /* XENO_COBALT_INTERNAL_H */

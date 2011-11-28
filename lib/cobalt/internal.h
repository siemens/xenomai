#ifndef XENO_COBALT_INTERNAL_H
#define XENO_COBALT_INTERNAL_H

#include <kernel/cobalt/mutex.h>
#include <kernel/cobalt/cond.h>

static inline unsigned long *cond_get_signalsp(struct __shadow_cond *shadow)
{
	if (likely(!shadow->attr.pshared))
		return shadow->pending_signals;

	return (unsigned long *)(xeno_sem_heap[1]
				 + shadow->pending_signals_offset);
}

static inline struct mutex_dat *
cond_get_mutex_datp(struct __shadow_cond *shadow)
{
	if (shadow->mutex_datp == (struct mutex_dat *)~0UL)
		return NULL;

	if (likely(!shadow->attr.pshared))
		return shadow->mutex_datp;

	return (struct mutex_dat *)(xeno_sem_heap[1]
				    + shadow->mutex_datp_offset);
}

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

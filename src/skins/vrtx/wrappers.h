#ifndef _XENO_VRTX_WRAPPERS_H
#define _XENO_VRTX_WRAPPERS_H

#include <sys/types.h>
#include <pthread.h>
#include <semaphore.h>

int __real_pthread_create(pthread_t *tid,
			  const pthread_attr_t * attr,
			  void *(*start) (void *), void *arg);

int __real_pthread_setschedparam(pthread_t thread,
				 int policy, const struct sched_param *param);

int __real_sem_init(sem_t *sem, int pshared, unsigned value);

int __real_sem_destroy(sem_t *sem);

int __real_sem_post(sem_t *sem);

int __real_sem_wait(sem_t *sem);

#endif /* !_XENO_VRTX_WRAPPERS_H */

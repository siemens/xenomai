#ifndef _TARGET_H_
#define _TARGET_H_

#include <pthread.h>
#include "copperplate/wrappers.h"

#define TLSF_MLOCK_T            pthread_mutex_t
#define TLSF_CREATE_LOCK(l)     __RT(pthread_mutex_init (l, NULL))
#define TLSF_DESTROY_LOCK(l)    __RT(pthread_mutex_destroy(l))
#define TLSF_ACQUIRE_LOCK(l)    __RT(pthread_mutex_lock(l))
#define TLSF_RELEASE_LOCK(l)    __RT(pthread_mutex_unlock(l))

#endif

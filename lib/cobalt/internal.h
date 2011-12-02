#ifndef XENO_COBALT_INTERNAL_H
#define XENO_COBALT_INTERNAL_H

#include <pthread.h>
#include <asm-generic/bits/current.h>

struct cobalt_threadstat;
struct cobalt_monitor_shadow;

void __cobalt_thread_harden(void);

int __cobalt_thread_stat(pthread_t tid,
			 struct cobalt_threadstat *stat);

int cobalt_monitor_init(cobalt_monitor_t *mon, int flags);

int cobalt_monitor_destroy(cobalt_monitor_t *mon);

int cobalt_monitor_enter(cobalt_monitor_t *mon);

int cobalt_monitor_exit(cobalt_monitor_t *mon);

int cobalt_monitor_wait(cobalt_monitor_t *mon, int event,
			const struct timespec *ts);

void cobalt_monitor_grant(cobalt_monitor_t *mon,
			  unsigned long *u_mode);

int cobalt_monitor_grant_sync(cobalt_monitor_t *mon,
			      unsigned long *u_mode);

void cobalt_monitor_grant_all(cobalt_monitor_t *mon,
			      unsigned long *u_mode);

int cobalt_monitor_grant_all_sync(cobalt_monitor_t *mon,
				  unsigned long *u_mode);

void cobalt_monitor_drain(cobalt_monitor_t *mon);

int cobalt_monitor_drain_sync(cobalt_monitor_t *mon);

void cobalt_monitor_drain_all(cobalt_monitor_t *mon);

int cobalt_monitor_drain_all_sync(cobalt_monitor_t *mon);

#endif /* XENO_COBALT_INTERNAL_H */

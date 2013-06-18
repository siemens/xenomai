#ifndef XENO_COBALT_INTERNAL_H
#define XENO_COBALT_INTERNAL_H

#include <signal.h>
#include <pthread.h>
#include <asm-generic/current.h>

#define report_error(fmt, args...) \
	__STD(fprintf(stderr, "Xenomai/cobalt: " fmt "\n", ##args))

#define report_error_cont(fmt, args...) \
	__STD(fprintf(stderr, "                " fmt "\n", ##args))

void cobalt_sigshadow_install_once(void);

extern int __cobalt_muxid;

struct xnthread_user_window;
struct cobalt_threadstat;
struct cobalt_monitor_shadow;

void ___cobalt_prefault(void *p, size_t len);

#define __cobalt_prefault(p) ___cobalt_prefault(p, sizeof(*p))

void __cobalt_thread_harden(void);

int __cobalt_thread_stat(pid_t pid,
			 struct cobalt_threadstat *stat);

int cobalt_monitor_init(cobalt_monitor_t *mon, int flags);

int cobalt_monitor_destroy(cobalt_monitor_t *mon);

int cobalt_monitor_enter(cobalt_monitor_t *mon);

int cobalt_monitor_exit(cobalt_monitor_t *mon);

int cobalt_monitor_wait(cobalt_monitor_t *mon, int event,
			const struct timespec *ts);

void cobalt_monitor_grant(cobalt_monitor_t *mon,
			  struct xnthread_user_window *u_window);

int cobalt_monitor_grant_sync(cobalt_monitor_t *mon,
			      struct xnthread_user_window *u_window);

void cobalt_monitor_grant_all(cobalt_monitor_t *mon);

int cobalt_monitor_grant_all_sync(cobalt_monitor_t *mon);

void cobalt_monitor_drain(cobalt_monitor_t *mon);

int cobalt_monitor_drain_sync(cobalt_monitor_t *mon);

void cobalt_monitor_drain_all(cobalt_monitor_t *mon);

int cobalt_monitor_drain_all_sync(cobalt_monitor_t *mon);

int cobalt_event_init(cobalt_event_t *event,
		      unsigned long value,
		      int flags);

int cobalt_event_post(cobalt_event_t *event,
		      unsigned long bits);

int cobalt_event_wait(cobalt_event_t *event,
		      unsigned long bits,
		      unsigned long *bits_r,
		      int mode,
		      const struct timespec *timeout);

unsigned long cobalt_event_clear(cobalt_event_t *event,
				 unsigned long bits);

int cobalt_event_inquire(cobalt_event_t *event,
			 unsigned long *bits_r);

int cobalt_event_destroy(cobalt_event_t *event);

void cobalt_print_init(void);

void cobalt_print_exit(void);

void cobalt_handle_sigdebug(int sig, siginfo_t *si, void *context);

#endif /* XENO_COBALT_INTERNAL_H */

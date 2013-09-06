/*
 * Copyright (C) 2008 Philippe Gerum <rpm@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */
#ifndef _LIB_COBALT_INTERNAL_H
#define _LIB_COBALT_INTERNAL_H

#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <nocore/atomic.h>
#include <cobalt/uapi/kernel/synch.h>
#include <cobalt/uapi/kernel/vdso.h>
#include <cobalt/uapi/mutex.h>
#include <cobalt/uapi/event.h>
#include <cobalt/uapi/monitor.h>
#include <cobalt/uapi/thread.h>
#include <cobalt/uapi/cond.h>
#include <xeno_config.h>
#include "current.h"

#define report_error(fmt, args...) \
	__STD(fprintf(stderr, "Xenomai/cobalt: " fmt "\n", ##args))

#define report_error_cont(fmt, args...) \
	__STD(fprintf(stderr, "                " fmt "\n", ##args))

void cobalt_sigshadow_install_once(void);

extern unsigned long cobalt_sem_heap[2];

static inline struct mutex_dat *mutex_get_datp(struct __shadow_mutex *shadow)
{
	if (shadow->attr.pshared)
		return (struct mutex_dat *)(cobalt_sem_heap[1] + shadow->dat_offset);

	return shadow->dat;
}

static inline atomic_long_t *mutex_get_ownerp(struct __shadow_mutex *shadow)
{
	return &mutex_get_datp(shadow)->owner;
}

size_t cobalt_get_stacksize(size_t size);

void ___cobalt_prefault(void *p, size_t len);
#define __cobalt_prefault(p) ___cobalt_prefault(p, sizeof(*p))

void __cobalt_thread_harden(void);

int __cobalt_thread_stat(pid_t pid,
			 struct cobalt_threadstat *stat);

int __cobalt_serial_debug(const char *fmt, ...);

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

void cobalt_ticks_init(unsigned long long freq);

void cobalt_handle_sigdebug(int sig, siginfo_t *si, void *context);

struct xnfeatinfo;

void cobalt_check_features(struct xnfeatinfo *finfo);

extern pthread_t __cobalt_main_tid;

extern struct sigaction __cobalt_orig_sigdebug;

extern int __cobalt_muxid;

#endif /* _LIB_COBALT_INTERNAL_H */

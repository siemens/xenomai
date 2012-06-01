/*
 * Copyright (C) 2011 Philippe Gerum <rpm@xenomai.org>.
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
 *
 * --
 * Internal Cobalt services. No sanity check will be done with
 * respect to object validity, callers have to take care of this.
 */

#include <sys/types.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <nucleus/synch.h>
#include <nucleus/thread.h>
#include <cobalt/syscall.h>
#include <kernel/cobalt/monitor.h>
#include <kernel/cobalt/event.h>
#include <asm-generic/bits/current.h>
#include "internal.h"

extern unsigned long xeno_sem_heap[2];

void __cobalt_thread_harden(void)
{
	unsigned long status = xeno_get_current_mode();

	/* non-RT shadows are NOT allowed to force primary mode. */
	if ((status & (XNRELAX|XNOTHER)) == XNRELAX)
		XENOMAI_SYSCALL1(sc_nucleus_migrate, XENOMAI_XENO_DOMAIN);
}

int __cobalt_thread_stat(pthread_t tid, struct cobalt_threadstat *stat)
{
	return XENOMAI_SKINCALL2(__cobalt_muxid,
				 sc_cobalt_thread_getstat, tid, stat);
}

void ___cobalt_prefault(void *p, size_t len)
{
	volatile char *_p = (volatile char *)p, *end;
	long pagesz = sysconf(_SC_PAGESIZE);

	end = _p + len;
	do {
		*_p = *_p;
		_p += pagesz;
	} while (_p < end);
}

static inline
struct cobalt_monitor_data *get_monitor_data(cobalt_monitor_t *mon)
{
	return mon->flags & COBALT_MONITOR_SHARED ?
		(void *)xeno_sem_heap[1] + mon->u.data_offset :
		mon->u.data;
}

int cobalt_monitor_init(cobalt_monitor_t *mon, int flags)
{
	struct cobalt_monitor_data *datp;
	int ret;

	ret = XENOMAI_SKINCALL2(__cobalt_muxid,
				sc_cobalt_monitor_init,
				mon, flags);
	if (ret)
		return ret;

	if ((flags & COBALT_MONITOR_SHARED) == 0) {
		datp = (void *)xeno_sem_heap[0] + mon->u.data_offset;
		mon->u.data = datp;
	} else
		datp = get_monitor_data(mon);

	__cobalt_prefault(datp);

	return 0;
}

int cobalt_monitor_destroy(cobalt_monitor_t *mon)
{
	return XENOMAI_SKINCALL1(__cobalt_muxid,
				 sc_cobalt_monitor_destroy,
				 mon);
}

int cobalt_monitor_enter(cobalt_monitor_t *mon)
{
	struct cobalt_monitor_data *datp;
	unsigned long status;
	xnhandle_t cur;
	int ret;

	/*
	 * Assumptions on entry:
	 *
	 * - this is a Xenomai shadow (caller checked this).
	 * - no recursive entry/locking.
	 */

	status = xeno_get_current_mode();
	if (status & (XNRELAX|XNOTHER))
		goto syscall;

	datp = get_monitor_data(mon);
	cur = xeno_get_current();
	ret = xnsynch_fast_acquire(&datp->owner, cur);
	if (ret == 0) {
		datp->flags &= ~(COBALT_MONITOR_SIGNALED|COBALT_MONITOR_BROADCAST);
		return 0;
	}
syscall:
	/*
	 * Jump to kernel to wait for entry. We redo in case of
	 * interrupt.
	 */
	do
		ret = XENOMAI_SKINCALL1(__cobalt_muxid,
					sc_cobalt_monitor_enter,
					mon);
	while (ret == -EINTR);

	return ret;
}

int cobalt_monitor_exit(cobalt_monitor_t *mon)
{
	struct cobalt_monitor_data *datp;
	unsigned long status;
	xnhandle_t cur;

	__sync_synchronize();

	datp = get_monitor_data(mon);
	if ((datp->flags & COBALT_MONITOR_PENDED) &&
	    (datp->flags & COBALT_MONITOR_SIGNALED))
		goto syscall;

	status = xeno_get_current_mode();
	if (status & XNOTHER)
		goto syscall;

	cur = xeno_get_current();
	if (xnsynch_fast_release(&datp->owner, cur))
		return 0;
syscall:
	return XENOMAI_SKINCALL1(__cobalt_muxid,
				 sc_cobalt_monitor_exit,
				 mon);
}

int cobalt_monitor_wait(cobalt_monitor_t *mon, int event,
			const struct timespec *ts)
{
	int ret, opret, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	ret = XENOMAI_SKINCALL4(__cobalt_muxid,
				sc_cobalt_monitor_wait,
				mon, event, ts, &opret);

	pthread_setcanceltype(oldtype, NULL);

	/*
	 * If we got interrupted while trying to re-enter the monitor,
	 * we need to redo. In the meantime, any pending linux signal
	 * has been processed.
	 */
	if (ret == -EINTR)
		ret = cobalt_monitor_enter(mon);

	return ret ?: opret;
}

void cobalt_monitor_grant(cobalt_monitor_t *mon,
			  struct xnthread_user_window *u_window)
{
	struct cobalt_monitor_data *datp = get_monitor_data(mon);

	datp->flags |= COBALT_MONITOR_GRANTED;
	u_window->grant_value = 1;
}

int cobalt_monitor_grant_sync(cobalt_monitor_t *mon,
			  struct xnthread_user_window *u_window)
{
	struct cobalt_monitor_data *datp = get_monitor_data(mon);
	int ret, oldtype;

	cobalt_monitor_grant(mon, u_window);

	if ((datp->flags & COBALT_MONITOR_PENDED) == 0)
		return 0;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	ret = XENOMAI_SKINCALL1(__cobalt_muxid,
				sc_cobalt_monitor_sync,
				mon);

	pthread_setcanceltype(oldtype, NULL);

	if (ret == -EINTR)
		return cobalt_monitor_enter(mon);

	return ret;
}

void cobalt_monitor_grant_all(cobalt_monitor_t *mon)
{
	struct cobalt_monitor_data *datp = get_monitor_data(mon);

	datp->flags |= COBALT_MONITOR_GRANTED|COBALT_MONITOR_BROADCAST;
}

int cobalt_monitor_grant_all_sync(cobalt_monitor_t *mon)
{
	struct cobalt_monitor_data *datp = get_monitor_data(mon);
	int ret, oldtype;

	cobalt_monitor_grant_all(mon);

	if ((datp->flags & COBALT_MONITOR_PENDED) == 0)
		return 0;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	ret = XENOMAI_SKINCALL1(__cobalt_muxid,
				sc_cobalt_monitor_sync,
				mon);

	pthread_setcanceltype(oldtype, NULL);

	if (ret == -EINTR)
		return cobalt_monitor_enter(mon);

	return ret;
}

void cobalt_monitor_drain(cobalt_monitor_t *mon)
{
	struct cobalt_monitor_data *datp = get_monitor_data(mon);

	datp->flags |= COBALT_MONITOR_DRAINED;
}

int cobalt_monitor_drain_sync(cobalt_monitor_t *mon)
{
	struct cobalt_monitor_data *datp = get_monitor_data(mon);
	int ret, oldtype;

	cobalt_monitor_drain(mon);

	if ((datp->flags & COBALT_MONITOR_PENDED) == 0)
		return 0;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	ret = XENOMAI_SKINCALL1(__cobalt_muxid,
				sc_cobalt_monitor_sync,
				mon);

	pthread_setcanceltype(oldtype, NULL);

	if (ret == -EINTR)
		return cobalt_monitor_enter(mon);

	return ret;
}

void cobalt_monitor_drain_all(cobalt_monitor_t *mon)
{
	struct cobalt_monitor_data *datp = get_monitor_data(mon);

	datp->flags |= COBALT_MONITOR_DRAINED|COBALT_MONITOR_BROADCAST;
}

int cobalt_monitor_drain_all_sync(cobalt_monitor_t *mon)
{
	struct cobalt_monitor_data *datp = get_monitor_data(mon);
	int ret, oldtype;

	cobalt_monitor_drain_all(mon);

	if ((datp->flags & COBALT_MONITOR_PENDED) == 0)
		return 0;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	ret = XENOMAI_SKINCALL1(__cobalt_muxid,
				sc_cobalt_monitor_sync,
				mon);

	pthread_setcanceltype(oldtype, NULL);

	if (ret == -EINTR)
		return cobalt_monitor_enter(mon);

	return ret;
}

void cobalt_handle_sigdebug(int sig, siginfo_t *si, void *context)
{
	struct sigaction sa;

	if (si->si_value.sival_int == SIGDEBUG_NOMLOCK) {
		fprintf(stderr, "Xenomai: process memory not locked "
			"(missing mlockall?)\n");
		fflush(stderr);
		exit(4);
	}

	/*
	 * XNTRAPSW was set for the thread but no user-defined handler
	 * has been set to override our internal handler, so let's
	 * invoke the default signal action.
	 */
	sa.sa_handler = SIG_DFL;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGXCPU, &sa, NULL);
	pthread_kill(pthread_self(), SIGXCPU);
}

static inline
struct cobalt_event_data *get_event_data(cobalt_event_t *event)
{
	return event->flags & COBALT_EVENT_SHARED ?
		(void *)xeno_sem_heap[1] + event->u.data_offset :
		event->u.data;
}

int cobalt_event_init(cobalt_event_t *event, unsigned long value,
		      int flags)
{
	struct cobalt_event_data *datp;
	int ret;

	ret = XENOMAI_SKINCALL3(__cobalt_muxid,
				sc_cobalt_event_init,
				event, value, flags);
	if (ret)
		return ret;

	if ((flags & COBALT_EVENT_SHARED) == 0) {
		datp = (void *)xeno_sem_heap[0] + event->u.data_offset;
		event->u.data = datp;
	} else
		datp = get_event_data(event);

	__cobalt_prefault(datp);

	return 0;
}

int cobalt_event_destroy(cobalt_event_t *event)
{
	return XENOMAI_SKINCALL1(__cobalt_muxid,
				 sc_cobalt_event_destroy,
				 event);
}

int cobalt_event_post(cobalt_event_t *event, unsigned long bits)
{
	struct cobalt_event_data *datp = get_event_data(event);

	if (bits == 0)
		return 0;

	__sync_or_and_fetch(&datp->value, bits); /* full barrier. */

	if ((datp->flags & COBALT_EVENT_PENDED) == 0)
		return 0;

	return XENOMAI_SKINCALL1(__cobalt_muxid,
				 sc_cobalt_event_sync, event);
}

int cobalt_event_wait(cobalt_event_t *event,
		      unsigned long bits, unsigned long *bits_r,
		      int mode, const struct timespec *timeout)
{
	int ret, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	ret = XENOMAI_SKINCALL5(__cobalt_muxid,
				sc_cobalt_event_wait,
				event, bits, bits_r, mode, timeout);

	pthread_setcanceltype(oldtype, NULL);

	return ret;
}

unsigned long cobalt_event_clear(cobalt_event_t *event,
				 unsigned long bits)
{
	struct cobalt_event_data *datp = get_event_data(event);

	return __sync_fetch_and_and(&datp->value, ~bits);
}

int cobalt_event_inquire(cobalt_event_t *event, unsigned long *bits_r)
{
	struct cobalt_event_data *datp = get_event_data(event);

	/*
	 * We don't guarantee clean readings, this service is
	 * primarily for debug purposes when the caller won't bet the
	 * house on the values returned.
	 */
	__sync_synchronize();
	*bits_r = datp->value;

	return datp->nwaiters;
}

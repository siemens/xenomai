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
 */

#include <sys/types.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <nucleus/synch.h>
#include <nucleus/thread.h>
#include <cobalt/syscall.h>
#include <kernel/cobalt/monitor.h>
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
	}

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

	xnarch_memory_barrier();

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

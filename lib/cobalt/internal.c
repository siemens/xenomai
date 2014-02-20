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
#include <limits.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <stdarg.h>
#include <pthread.h>
#include <asm/xenomai/syscall.h>
#include "current.h"
#include "internal.h"

void __cobalt_thread_harden(void)
{
	unsigned long status = cobalt_get_current_mode();

	/* non-RT shadows are NOT allowed to force primary mode. */
	if ((status & (XNRELAX|XNWEAK)) == XNRELAX)
		XENOMAI_SYSCALL1(sc_nucleus_migrate, XENOMAI_XENO_DOMAIN);
}

int __cobalt_thread_stat(pid_t pid, struct cobalt_threadstat *stat)
{
	return XENOMAI_SKINCALL2(__cobalt_muxid,
				 sc_cobalt_thread_getstat, pid, stat);
}

int __cobalt_thread_join(pthread_t thread)
{
	int ret, oldtype;

	/*
	 * Serialize with the regular task exit path, so that no call
	 * for the joined pthread may succeed after this routine
	 * returns. A successful call to sc_cobalt_thread_join
	 * receives -EIDRM, meaning that we eventually joined the
	 * exiting thread as seen by the Cobalt core.
	 *
	 * -ESRCH means that the joined thread has already exited
	 * linux-wise, while we were about to wait for it from the
	 * Cobalt side, in which case we are fine.
	 *
	 * -EBUSY denotes a multiple join for several threads in
	 * parallel to the same target.
	 *
	 * -EPERM may be received because the current context is not a
	 * Xenomai thread.
	 *
	 * -EINVAL is received in case the target is not a joinable
	 * thread (i.e. detached).
	 *
	 * Zero is unexpected.
	 *
	 * CAUTION: this service joins a thread Cobat-wise only, not
	 * glibc-wise.  For a complete join comprising the libc
	 * cleanups, __STD(pthread_join()) should be paired with this
	 * call.
	 */
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	do
		ret = XENOMAI_SKINCALL1(__cobalt_muxid,
					sc_cobalt_thread_join, thread);
	while (ret == -EINTR);

	pthread_setcanceltype(oldtype, NULL);

	return ret;
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

int __cobalt_serial_debug(const char *fmt, ...)
{
	char msg[128];
	va_list ap;
	int n, ret;

	/*
	 * The serial debug output handler disables hw IRQs while
	 * writing to the UART console port, so the message ought to
	 * be reasonably short.
	 */
	va_start(ap, fmt);
	n = vsnprintf(msg, sizeof(msg), fmt, ap);
	ret = XENOMAI_SYSCALL2(sc_nucleus_serialdbg, msg, n);
	va_end(ap);

	return ret;
}

size_t cobalt_get_stacksize(size_t size)
{
	static const size_t default_size = PTHREAD_STACK_MIN * 4;
	static size_t min_size;

	if (min_size == 0)
		min_size = PTHREAD_STACK_MIN + getpagesize();

	if (size == 0)
		size = default_size;

	if (size < min_size)
		size = min_size;

	return size;
}

static inline
struct cobalt_monitor_data *get_monitor_data(cobalt_monitor_t *mon)
{
	return mon->flags & COBALT_MONITOR_SHARED ?
		(void *)cobalt_sem_heap[1] + mon->u.data_offset :
		mon->u.data;
}

int cobalt_monitor_init(cobalt_monitor_t *mon, clockid_t clk_id, int flags)
{
	struct cobalt_monitor_data *datp;
	int ret;

	ret = XENOMAI_SKINCALL3(__cobalt_muxid,
				sc_cobalt_monitor_init,
				mon, clk_id, flags);
	if (ret)
		return ret;

	if ((flags & COBALT_MONITOR_SHARED) == 0) {
		datp = (void *)cobalt_sem_heap[0] + mon->u.data_offset;
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
	int ret, oldtype;
	xnhandle_t cur;

	/*
	 * Assumptions on entry:
	 *
	 * - this is a Xenomai shadow (caller checked this).
	 * - no recursive entry/locking.
	 */

	status = cobalt_get_current_mode();
	if (status & (XNRELAX|XNWEAK))
		goto syscall;

	datp = get_monitor_data(mon);
	cur = cobalt_get_current();
	ret = xnsynch_fast_acquire(&datp->owner, cur);
	if (ret == 0) {
		datp->flags &= ~(COBALT_MONITOR_SIGNALED|COBALT_MONITOR_BROADCAST);
		return 0;
	}
syscall:
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	/*
	 * Jump to kernel to wait for entry. We redo in case of
	 * interrupt.
	 */
	do
		ret = XENOMAI_SKINCALL1(__cobalt_muxid,
					sc_cobalt_monitor_enter,
					mon);
	while (ret == -EINTR);

	pthread_setcanceltype(oldtype, NULL);

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

	status = cobalt_get_current_mode();
	if (status & XNWEAK)
		goto syscall;

	cur = cobalt_get_current();
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

void cobalt_sigdebug_handler(int sig, siginfo_t *si, void *context)
{
	if (si->si_value.sival_int == SIGDEBUG_NOMLOCK) {
		fprintf(stderr, "Xenomai: process memory not locked "
			"(missing mlockall?)\n");
		fflush(stderr);
		exit(4);
	}

	/*
	 * XNTRAPSW was set for the thread but no user-defined handler
	 * has been set to override our internal handler, so let's
	 * restore the setting before we registered and re-raise the
	 * signal. Usually triggers the default signal action.
	 */
	sigaction(SIGXCPU, &__cobalt_orig_sigdebug, NULL);
	pthread_kill(pthread_self(), SIGXCPU);
}

static inline
struct cobalt_event_data *get_event_data(cobalt_event_t *event)
{
	return event->flags & COBALT_EVENT_SHARED ?
		(void *)cobalt_sem_heap[1] + event->u.data_offset :
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
		datp = (void *)cobalt_sem_heap[0] + event->u.data_offset;
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

int cobalt_event_inquire(cobalt_event_t *event,
			 struct cobalt_event_info *info,
			 pid_t *waitlist, size_t waitsz)
{
	return XENOMAI_SKINCALL4(__cobalt_muxid,
				 sc_cobalt_event_inquire, event,
				 info, waitlist, waitsz);
}

int cobalt_sem_inquire(sem_t *sem, struct cobalt_sem_info *info,
		       pid_t *waitlist, size_t waitsz)
{
	struct cobalt_sem_shadow *_sem = &((union cobalt_sem_union *)sem)->shadow_sem;
	
	return XENOMAI_SKINCALL4(__cobalt_muxid,
				 sc_cobalt_sem_inquire, _sem,
				 info, waitlist, waitsz);
}

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

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <semaphore.h>
#include <cobalt/syscall.h>
#include <kernel/cobalt/mutex.h>
#include <kernel/cobalt/cond.h>
#include <asm-generic/bits/current.h>

extern int __cobalt_muxid;

void __cobalt_thread_harden(void)
{
	unsigned long status = xeno_get_current_mode();

	/* non-RT shadows are NOT allowed to force primary mode. */
	if ((status & (XNRELAX|XNOTHER)) == XNRELAX)
		XENOMAI_SYSCALL1(__xn_sys_migrate, XENOMAI_XENO_DOMAIN);
}

int __cobalt_thread_stat(pthread_t tid, struct cobalt_threadstat *stat)
{
	return -XENOMAI_SKINCALL2(__cobalt_muxid,
				  __cobalt_thread_getstat, tid, stat);
}

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

int __cobalt_event_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
	struct __shadow_cond *_cnd;
	struct __shadow_mutex *_mx;
	unsigned int count;
	int err, _err = 0;

	_cnd = &((union __xeno_cond *)cond)->shadow_cond;
	_mx = &((union __xeno_mutex *)mutex)->shadow_mutex;
	count = _mx->lockcnt;

	err = XENOMAI_SKINCALL5(__cobalt_muxid,
				 __cobalt_cond_wait_prologue,
				 _cnd, _mx, &_err, 0, NULL);
	while (err == -EINTR)
		err = XENOMAI_SKINCALL2(__cobalt_muxid,
					__cobalt_cond_wait_epilogue, _cnd, _mx);

	_mx->lockcnt = count;

	return -err ?: -_err;
}

int __cobalt_event_timedwait(pthread_cond_t *cond,
			     pthread_mutex_t *mutex,
			     const struct timespec *abstime)
{
	struct __shadow_cond *_cnd;
	struct __shadow_mutex *_mx;
	unsigned int count;
	int err, _err = 0;

	_cnd = &((union __xeno_cond *)cond)->shadow_cond;
	_mx = &((union __xeno_mutex *)mutex)->shadow_mutex;
	count = _mx->lockcnt;

	err = XENOMAI_SKINCALL5(__cobalt_muxid,
				 __cobalt_cond_wait_prologue,
				 _cnd, _mx, &_err, 1, abstime);
	while (err == -EINTR)
		err = XENOMAI_SKINCALL2(__cobalt_muxid,
					__cobalt_cond_wait_epilogue, _cnd, _mx);

	_mx->lockcnt = count;

	return -err ?: -_err;
}

int __cobalt_event_signal(pthread_cond_t *cond)
{
	struct __shadow_cond *_cnd = &((union __xeno_cond *)cond)->shadow_cond;
	unsigned long pending_signals, *pending_signalsp;
	struct mutex_dat *mutex_datp;

	mutex_datp = cond_get_mutex_datp(_cnd);
	if (mutex_datp == NULL)
		return 0;

	mutex_datp->flags |= COBALT_MUTEX_COND_SIGNAL;

	pending_signalsp = cond_get_signalsp(_cnd);
	pending_signals = *pending_signalsp;
	if (pending_signals != ~0UL)
		*pending_signalsp = pending_signals + 1;

	return 0;
}

int __cobalt_event_broadcast(pthread_cond_t *cond)
{
	struct mutex_dat *mutex_datp;
	struct __shadow_cond *_cnd;

	_cnd = &((union __xeno_cond *)cond)->shadow_cond;

	mutex_datp = cond_get_mutex_datp(_cnd);
	if (mutex_datp == NULL)
		return 0;

	mutex_datp->flags |= COBALT_MUTEX_COND_SIGNAL;
	*cond_get_signalsp(_cnd) = ~0UL;

	return 0;
}

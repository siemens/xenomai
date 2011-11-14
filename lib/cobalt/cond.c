/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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

#include <errno.h>
#include <nucleus/synch.h>
#include <cobalt/syscall.h>
#include <kernel/cobalt/mutex.h>
#include <kernel/cobalt/cond.h>
#include <asm-generic/bits/current.h>

extern int __cobalt_muxid;

#define COBALT_COND_MAGIC 0x86860505

extern unsigned long xeno_sem_heap[2];

static unsigned long *get_signalsp(struct __shadow_cond *shadow)
{
	if (likely(!shadow->attr.pshared))
		return shadow->pending_signals;

	return (unsigned long *)(xeno_sem_heap[1]
				 + shadow->pending_signals_offset);
}

static xnarch_atomic_t *get_mutex_ownerp(struct __shadow_cond *shadow)
{
	if (shadow->mutex_ownerp == (xnarch_atomic_t *)~0UL)
		return NULL;

	if (likely(!shadow->attr.pshared))
		return shadow->mutex_ownerp;

	return (xnarch_atomic_t *)(xeno_sem_heap[1]
				   + shadow->mutex_ownerp_offset);
}

int __wrap_pthread_condattr_init(pthread_condattr_t *attr)
{
	return -XENOMAI_SKINCALL1(__cobalt_muxid, __cobalt_condattr_init, attr);
}

int __wrap_pthread_condattr_destroy(pthread_condattr_t *attr)
{
	return -XENOMAI_SKINCALL1(__cobalt_muxid,__cobalt_condattr_destroy,attr);
}

int __wrap_pthread_condattr_getclock(const pthread_condattr_t *attr,
				     clockid_t *clk_id)
{
	return -XENOMAI_SKINCALL2(__cobalt_muxid,
				  __cobalt_condattr_getclock, attr, clk_id);
}

int __wrap_pthread_condattr_setclock(pthread_condattr_t *attr,
				     clockid_t clk_id)
{
	return -XENOMAI_SKINCALL2(__cobalt_muxid,
				  __cobalt_condattr_setclock, attr, clk_id);
}

int __wrap_pthread_condattr_getpshared(const pthread_condattr_t *attr,
				       int *pshared)
{
	return -XENOMAI_SKINCALL2(__cobalt_muxid,
				  __cobalt_condattr_getpshared, attr, pshared);
}

int __wrap_pthread_condattr_setpshared(pthread_condattr_t *attr, int pshared)
{
	return -XENOMAI_SKINCALL2(__cobalt_muxid,
				  __cobalt_condattr_setpshared, attr, pshared);
}

int __wrap_pthread_cond_init(pthread_cond_t * cond,
			     const pthread_condattr_t * attr)
{
	struct __shadow_cond *shadow =
		&((union __xeno_cond *)cond)->shadow_cond;
	int err;

	err = XENOMAI_SKINCALL2(__cobalt_muxid,
				 __cobalt_cond_init, shadow, attr);
	if (!err && !shadow->attr.pshared) {
		shadow->pending_signals = (unsigned long *)
			(xeno_sem_heap[0] + shadow->pending_signals_offset);
	}

	return -err;
}

int __wrap_pthread_cond_destroy(pthread_cond_t * cond)
{
	union __xeno_cond *_cond = (union __xeno_cond *)cond;

	return -XENOMAI_SKINCALL1(__cobalt_muxid,
				  __cobalt_cond_destroy, &_cond->shadow_cond);
}

struct cobalt_cond_cleanup_t {
	union __xeno_cond *cond;
	union __xeno_mutex *mutex;
	unsigned count;
	int err;
};

static void __pthread_cond_cleanup(void *data)
{
	struct cobalt_cond_cleanup_t *c = (struct cobalt_cond_cleanup_t *) data;
	int err;

	do {
		err = XENOMAI_SKINCALL2(__cobalt_muxid,
					__cobalt_cond_wait_epilogue,
					&c->cond->shadow_cond,
					&c->mutex->shadow_mutex);
	} while (err == -EINTR);

	c->mutex->shadow_mutex.lockcnt = c->count;
}

int __wrap_pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
	struct cobalt_cond_cleanup_t c = {
		.cond = (union __xeno_cond *)cond,
		.mutex = (union __xeno_mutex *)mutex,
	};
	int err, oldtype;

	pthread_cleanup_push(&__pthread_cond_cleanup, &c);

	c.count = c.mutex->shadow_mutex.lockcnt;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	err = XENOMAI_SKINCALL5(__cobalt_muxid,
				 __cobalt_cond_wait_prologue,
				 &c.cond->shadow_cond,
				 &c.mutex->shadow_mutex, &c.err, 0, NULL);

	pthread_setcanceltype(oldtype, NULL);

	pthread_cleanup_pop(0);

	while (err == -EINTR)
		err = XENOMAI_SKINCALL2(__cobalt_muxid,
					 __cobalt_cond_wait_epilogue,
					 &c.cond->shadow_cond,
					 &c.mutex->shadow_mutex);

	c.mutex->shadow_mutex.lockcnt = c.count;

	pthread_testcancel();

	return -err ?: -c.err;
}

int __wrap_pthread_cond_timedwait(pthread_cond_t * cond,
				  pthread_mutex_t * mutex,
				  const struct timespec *abstime)
{
	struct cobalt_cond_cleanup_t c = {
		.cond = (union __xeno_cond *)cond,
		.mutex = (union __xeno_mutex *)mutex,
	};
	int err, oldtype;

	pthread_cleanup_push(&__pthread_cond_cleanup, &c);

	c.count = c.mutex->shadow_mutex.lockcnt;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	err = XENOMAI_SKINCALL5(__cobalt_muxid,
				__cobalt_cond_wait_prologue,
				&c.cond->shadow_cond,
				&c.mutex->shadow_mutex, &c.err, 1, abstime);
	pthread_setcanceltype(oldtype, NULL);

	pthread_cleanup_pop(0);

	while (err == -EINTR)
		err = XENOMAI_SKINCALL2(__cobalt_muxid,
					__cobalt_cond_wait_epilogue,
					&c.cond->shadow_cond,
					&c.mutex->shadow_mutex);

	c.mutex->shadow_mutex.lockcnt = c.count;

	pthread_testcancel();

	return -err ?: -c.err;
}

int __wrap_pthread_cond_signal(pthread_cond_t * cond)
{
	struct __shadow_cond *shadow =
		&((union __xeno_cond *)cond)->shadow_cond;
	unsigned long *pending_signals;
	xnarch_atomic_t *mutex_ownerp;
	xnhandle_t cur;

	cur = xeno_get_current();
	if (cur == XN_NO_HANDLE)
		return EPERM;

	if (shadow->magic != COBALT_COND_MAGIC)
		return EINVAL;

	mutex_ownerp = get_mutex_ownerp(shadow);
	if (mutex_ownerp) {
		if (xnsynch_fast_set_spares(mutex_ownerp, cur,
					    COBALT_MUTEX_COND_SIGNAL) < 0)
			return EPERM;

		pending_signals = get_signalsp(shadow);
		if (*pending_signals != ~0UL)
			++(*pending_signals);
	}

	return 0;
}

int __wrap_pthread_cond_broadcast(pthread_cond_t * cond)
{
	struct __shadow_cond *shadow =
		&((union __xeno_cond *)cond)->shadow_cond;
	unsigned long *pending_signals;
	xnarch_atomic_t *mutex_ownerp;
	xnhandle_t cur;

	cur = xeno_get_current();
	if (cur == XN_NO_HANDLE)
		return EPERM;

	if (shadow->magic != COBALT_COND_MAGIC)
		return EINVAL;

	mutex_ownerp = get_mutex_ownerp(shadow);
	if (mutex_ownerp) {
		if (xnsynch_fast_set_spares(mutex_ownerp, cur,
					    COBALT_MUTEX_COND_SIGNAL) < 0)
			return EPERM;

		pending_signals = get_signalsp(shadow);
		*get_signalsp(shadow) = ~0UL;
	}

	return 0;
}

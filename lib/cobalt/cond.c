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
#include <pthread.h>
#include <asm/xenomai/syscall.h>
#include "current.h"
#include "internal.h"

/**
 * @ingroup cobalt_api
 * @defgroup cobalt_api_cond Condition variables
 *
 * Cobalt/POSIX condition variable services
 *
 * A condition variable is a synchronization object that allows threads to
 * suspend execution until some predicate on shared data is satisfied. The basic
 * operations on conditions are: signal the condition (when the predicate
 * becomes true), and wait for the condition, suspending the thread execution
 * until another thread signals the condition.
 *
 * A condition variable must always be associated with a mutex, to avoid the
 * race condition where a thread prepares to wait on a condition variable and
 * another thread signals the condition just before the first thread actually
 * waits on it.
 *
 * Before it can be used, a condition variable has to be initialized with
 * pthread_cond_init(). An attribute object, which reference may be passed to
 * this service, allows to select the features of the created condition
 * variable, namely the @a clock used by the pthread_cond_timedwait() service
 * (@a CLOCK_REALTIME is used by default), and whether it may be shared between
 * several processes (it may not be shared by default, see
 * pthread_condattr_setpshared()).
 *
 * Note that only pthread_cond_init() may be used to initialize a condition
 * variable, using the static initializer @a PTHREAD_COND_INITIALIZER is
 * not supported.
 *
 *@{
 */

static pthread_condattr_t cobalt_default_condattr;

static inline unsigned long *cond_get_signalsp(struct cobalt_cond_shadow *shadow)
{
	if (shadow->attr.pshared)
		return (unsigned long *)(cobalt_sem_heap[1]
					 + shadow->pending_signals_offset);

	return shadow->pending_signals;
}

static inline struct mutex_dat *
cond_get_mutex_datp(struct cobalt_cond_shadow *shadow)
{
	if (shadow->mutex_datp == (struct mutex_dat *)~0UL)
		return NULL;

	if (shadow->attr.pshared)
		return (struct mutex_dat *)(cobalt_sem_heap[1]
					    + shadow->mutex_datp_offset);

	return shadow->mutex_datp;
}

void cobalt_default_condattr_init(void)
{
	pthread_condattr_init(&cobalt_default_condattr);
}


/**
 * @fn int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
 * @brief Initialize a condition variable
 *
 * This service initializes the condition variable @a cond, using the condition
 * variable attributes object @a attr. If @a attr is @a NULL or this service is
 * called from user-space, default attributes are used (see
 * pthread_condattr_init()).
 *
 * @param cond the condition variable to be initialized;
 *
 * @param attr the condition variable attributes object.
 *
 * @return 0 on succes,
 * @return an error number if:
 * - EINVAL, the condition variable attributes object @a attr is invalid or
 *   uninitialized;
 * - EBUSY, the condition variable @a cond was already initialized;
 * - ENOMEM, insufficient memory exists in the system heap to initialize the
 *   condition variable, increase CONFIG_XENO_OPT_SYS_HEAPSZ.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_cond_init.html">
 * Specification.</a>
 */
COBALT_IMPL(int, pthread_cond_init, (pthread_cond_t *cond,
				     const pthread_condattr_t * attr))
{
	struct cobalt_cond_shadow *_cnd = &((union cobalt_cond_union *)cond)->shadow_cond;
	unsigned long *pending_signalsp;
	struct cobalt_condattr kcattr;
	int err, tmp;

	if (attr == NULL)
		attr = &cobalt_default_condattr;

	err = pthread_condattr_getpshared(attr, &tmp);
	if (err)
		return err;
	kcattr.pshared = tmp;

	err = pthread_condattr_getclock(attr, &tmp);
	if (err)
		return err;
	kcattr.clock = tmp;

	err = -XENOMAI_SKINCALL2(__cobalt_muxid, sc_cobalt_cond_init, _cnd, &kcattr);
	if (err)
		return err;

	if (!_cnd->attr.pshared) {
		pending_signalsp = (unsigned long *)
			(cobalt_sem_heap[0] + _cnd->pending_signals_offset);
		_cnd->pending_signals = pending_signalsp;
	} else
		pending_signalsp = cond_get_signalsp(_cnd);

	__cobalt_prefault(pending_signalsp);

	return 0;
}

/**
 * @fn int pthread_cond_destroy(pthread_cond_t *cond)
 * @brief Destroy a condition variable
 *
 * This service destroys the condition variable @a cond, if no thread is
 * currently blocked on it. The condition variable becomes invalid for all
 * condition variable services (they all return the EINVAL error) except
 * pthread_cond_init().
 *
 * @param cond the condition variable to be destroyed.
 *
 * @return 0 on succes,
 * @return an error number if:
 * - EINVAL, the condition variable @a cond is invalid;
 * - EPERM, the condition variable is not process-shared and does not belong to
 *   the current process;
 * - EBUSY, some thread is currently using the condition variable.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_cond_destroy.html">
 * Specification.</a>
 *
 */
COBALT_IMPL(int, pthread_cond_destroy, (pthread_cond_t *cond))
{
	struct cobalt_cond_shadow *_cond = &((union cobalt_cond_union *)cond)->shadow_cond;

	return -XENOMAI_SKINCALL1(__cobalt_muxid, sc_cobalt_cond_destroy, _cond);
}

struct cobalt_cond_cleanup_t {
	struct cobalt_cond_shadow *cond;
	struct cobalt_mutex_shadow *mutex;
	unsigned count;
	int err;
};

static void __pthread_cond_cleanup(void *data)
{
	struct cobalt_cond_cleanup_t *c = (struct cobalt_cond_cleanup_t *)data;
	int err;

	do {
		err = XENOMAI_SKINCALL2(__cobalt_muxid,
					sc_cobalt_cond_wait_epilogue,
					c->cond, c->mutex);
	} while (err == -EINTR);

	c->mutex->lockcnt = c->count;
}

COBALT_IMPL(int, pthread_cond_wait, (pthread_cond_t *cond, pthread_mutex_t *mutex))
{
	struct cobalt_cond_shadow *_cnd = &((union cobalt_cond_union *)cond)->shadow_cond;
	struct cobalt_mutex_shadow *_mx =
		&((union cobalt_mutex_union *)mutex)->shadow_mutex;
	struct cobalt_cond_cleanup_t c = {
		.cond = _cnd,
		.mutex = _mx,
		.err = 0,
	};
	int err, oldtype;
	unsigned count;

	if (_mx->magic != COBALT_MUTEX_MAGIC
	    || _cnd->magic != COBALT_COND_MAGIC)
		return EINVAL;

	if (_mx->attr.type == PTHREAD_MUTEX_ERRORCHECK) {
		xnhandle_t cur = cobalt_get_current();

		if (cur == XN_NO_HANDLE)
			return EPERM;

		if (xnsynch_fast_owner_check(mutex_get_ownerp(_mx), cur))
			return EPERM;
	}

	pthread_cleanup_push(&__pthread_cond_cleanup, &c);

	count = _mx->lockcnt;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	err = XENOMAI_SKINCALL5(__cobalt_muxid,
				 sc_cobalt_cond_wait_prologue,
				 _cnd, _mx, &c.err, 0, NULL);

	pthread_setcanceltype(oldtype, NULL);

	pthread_cleanup_pop(0);

	while (err == -EINTR)
		err = XENOMAI_SKINCALL2(__cobalt_muxid,
					sc_cobalt_cond_wait_epilogue, _cnd, _mx);

	_mx->lockcnt = count;

	pthread_testcancel();

	return -err ?: -c.err;
}

COBALT_IMPL(int, pthread_cond_timedwait, (pthread_cond_t *cond,
					  pthread_mutex_t *mutex,
					  const struct timespec *abstime))
{
	struct cobalt_cond_shadow *_cnd = &((union cobalt_cond_union *)cond)->shadow_cond;
	struct cobalt_mutex_shadow *_mx =
		&((union cobalt_mutex_union *)mutex)->shadow_mutex;
	struct cobalt_cond_cleanup_t c = {
		.cond = _cnd,
		.mutex = _mx,
	};
	int err, oldtype;
	unsigned count;

	if (_mx->magic != COBALT_MUTEX_MAGIC
	    || _cnd->magic != COBALT_COND_MAGIC)
		return EINVAL;

	if (_mx->attr.type == PTHREAD_MUTEX_ERRORCHECK) {
		xnhandle_t cur = cobalt_get_current();

		if (cur == XN_NO_HANDLE)
			return EPERM;

		if (xnsynch_fast_owner_check(mutex_get_ownerp(_mx), cur))
			return EPERM;
	}

	pthread_cleanup_push(&__pthread_cond_cleanup, &c);

	count = _mx->lockcnt;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	err = XENOMAI_SKINCALL5(__cobalt_muxid,
				sc_cobalt_cond_wait_prologue,
				_cnd, _mx, &c.err, 1, abstime);
	pthread_setcanceltype(oldtype, NULL);

	pthread_cleanup_pop(0);

	while (err == -EINTR)
		err = XENOMAI_SKINCALL2(__cobalt_muxid,
					sc_cobalt_cond_wait_epilogue, _cnd, _mx);

	_mx->lockcnt = count;

	pthread_testcancel();

	return -err ?: -c.err;
}

COBALT_IMPL(int, pthread_cond_signal, (pthread_cond_t *cond))
{
	struct cobalt_cond_shadow *_cnd = &((union cobalt_cond_union *)cond)->shadow_cond;
	unsigned long pending_signals, *pending_signalsp;
	struct mutex_dat *mutex_datp;
	unsigned long flags;
	xnhandle_t cur;

	if (_cnd->magic != COBALT_COND_MAGIC)
		return EINVAL;

	mutex_datp = cond_get_mutex_datp(_cnd);
	if (mutex_datp) {
		flags = mutex_datp->flags;
		if (flags & COBALT_MUTEX_ERRORCHECK) {
			cur = cobalt_get_current();
			if (cur == XN_NO_HANDLE)
				return EPERM;

			if (xnsynch_fast_owner_check(&mutex_datp->owner, cur) < 0)
				return EPERM;
		}
		mutex_datp->flags = flags | COBALT_MUTEX_COND_SIGNAL;
		pending_signalsp = cond_get_signalsp(_cnd);
		pending_signals = *pending_signalsp;
		if (pending_signals != ~0UL)
			*pending_signalsp = pending_signals + 1;
	}

	return 0;
}

COBALT_IMPL(int, pthread_cond_broadcast, (pthread_cond_t *cond))
{
	struct cobalt_cond_shadow *_cnd = &((union cobalt_cond_union *)cond)->shadow_cond;
	struct mutex_dat *mutex_datp;
	unsigned long flags;
	xnhandle_t cur;

	if (_cnd->magic != COBALT_COND_MAGIC)
		return EINVAL;

	mutex_datp = cond_get_mutex_datp(_cnd);
	if (mutex_datp) {
		flags = mutex_datp->flags ;
		if (flags & COBALT_MUTEX_ERRORCHECK) {
			cur = cobalt_get_current();
			if (cur == XN_NO_HANDLE)
				return EPERM;

			if (xnsynch_fast_owner_check(&mutex_datp->owner, cur) < 0)
				return EPERM;
		}
		mutex_datp->flags = flags | COBALT_MUTEX_COND_SIGNAL;
		*cond_get_signalsp(_cnd) = ~0UL;
	}

	return 0;
}

/** @}*/

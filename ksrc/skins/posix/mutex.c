/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/**
 * @ingroup posix
 * @defgroup posix_mutex Mutex services.
 *
 * Mutex services.
 *
 * A mutex is a MUTual EXclusion device, and is useful for protecting
 * shared data structures from concurrent modifications, and implementing
 * critical sections and monitors.
 *
 * A mutex has two possible states: unlocked (not owned by any thread), and
 * locked (owned by one thread). A mutex can never be owned by two different
 * threads simultaneously. A thread attempting to lock a mutex that is already
 * locked by another thread is suspended until the owning thread unlocks the
 * mutex first.
 *
 * Before it can be used, a mutex has to be initialized with
 * pthread_mutex_init(). An attribute object, which reference may be passed to
 * this service, allows to select the features of the created mutex, namely its
 * @a type (see pthread_mutexattr_settype()), the priority @a protocol it
 * uses (see pthread_mutexattr_setprotocol()) and whether it may be shared
 * between several processes (see pthread_mutexattr_setpshared()).
 *
 * By default, Xenomai POSIX skin mutexes are of the normal type, use no
 * priority protocol and may not be shared between several processes.
 *
 * Note that only pthread_mutex_init() may be used to initialize a mutex, using
 * the static initializer @a PTHREAD_MUTEX_INITIALIZER is not supported.
 *
 *@{*/

#include <nucleus/sys_ppd.h>
#include <posix/mutex.h>

pthread_mutexattr_t pse51_default_mutex_attr;

int pse51_mutex_check_init(struct __shadow_mutex *shadow,
			   const pthread_mutexattr_t *attr)
{
	xnqueue_t *mutexq;

	if (!attr)
		attr = &pse51_default_mutex_attr;

	mutexq = &pse51_kqueues(attr->pshared)->mutexq;

	if (shadow->magic == PSE51_MUTEX_MAGIC) {
		xnholder_t *holder;
		spl_t s;

		xnlock_get_irqsave(&nklock, s);
		for (holder = getheadq(mutexq); holder;
		     holder = nextq(mutexq, holder))
			if (holder == &shadow->mutex->link) {
				xnlock_put_irqrestore(&nklock, s);
				/* mutex is already in the queue. */
				return -EBUSY;
			}
		xnlock_put_irqrestore(&nklock, s);
	}

	return 0;
}

int pse51_mutex_init_internal(struct __shadow_mutex *shadow,
			      pse51_mutex_t *mutex,
			      xnarch_atomic_t *ownerp,
			      const pthread_mutexattr_t *attr)
{
	xnflags_t synch_flags = XNSYNCH_PRIO | XNSYNCH_OWNER;
	struct xnsys_ppd *sys_ppd;
	pse51_kqueues_t *kq;
	spl_t s;

	if (!attr)
		attr = &pse51_default_mutex_attr;

	if (attr->magic != PSE51_MUTEX_ATTR_MAGIC)
		return -EINVAL;

	kq = pse51_kqueues(attr->pshared);
	sys_ppd = xnsys_ppd_get(attr->pshared);

	shadow->magic = PSE51_MUTEX_MAGIC;
	shadow->mutex = mutex;
	shadow->lockcnt = 0;
	xnarch_atomic_set(&shadow->lock, -1);

#ifdef CONFIG_XENO_FASTSYNCH
	shadow->attr = *attr;
	shadow->owner_offset = xnheap_mapped_offset(&sys_ppd->sem_heap, ownerp);
#endif /* CONFIG_XENO_FASTSYNCH */

	if (attr->protocol == PTHREAD_PRIO_INHERIT)
		synch_flags |= XNSYNCH_PIP;

	mutex->magic = PSE51_MUTEX_MAGIC;
	xnsynch_init(&mutex->synchbase, synch_flags, ownerp);
	inith(&mutex->link);
	mutex->attr = *attr;
	mutex->owningq = kq;

	xnlock_get_irqsave(&nklock, s);
	appendq(&kq->mutexq, &mutex->link);
	xnlock_put_irqrestore(&nklock, s);

	return 0;
}

/**
 * Initialize a mutex.
 *
 * This services initializes the mutex @a mx, using the mutex attributes object
 * @a attr. If @a attr is @a NULL, default attributes are used (see
 * pthread_mutexattr_init()).
 *
 * @param mx the mutex to be initialized;
 *
 * @param attr the mutex attributes object.
 *
 * @return 0 on success,
 * @return an error number if:
 * - EINVAL, the mutex attributes object @a attr is invalid or uninitialized;
 * - EBUSY, the mutex @a mx was already initialized;
 * - ENOMEM, insufficient memory exists in the system heap to initialize the
 *   mutex, increase CONFIG_XENO_OPT_SYS_HEAPSZ.
 * - EAGAIN, insufficient memory exists in the semaphore heap to initialize the
 *   mutex, increase CONFIG_XENO_OPT_GLOBAL_SEM_HEAPSZ for a process-shared
 *   mutex, or CONFG_XENO_OPT_SEM_HEAPSZ for a process-private mutex.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_mutex_init.html">
 * Specification.</a>
 *
 */
int pthread_mutex_init(pthread_mutex_t *mx, const pthread_mutexattr_t *attr)
{
	struct __shadow_mutex *shadow =
	    &((union __xeno_mutex *)mx)->shadow_mutex;
	DECLARE_CB_LOCK_FLAGS(s);
	pse51_mutex_t *mutex;
	xnarch_atomic_t *ownerp = NULL;
	int err;

	if (!attr)
		attr = &pse51_default_mutex_attr;

	if (unlikely(cb_try_read_lock(&shadow->lock, s)))
		goto checked;

	err = pse51_mutex_check_init(shadow, attr);
#ifndef CONFIG_XENO_FASTSYNCH
	cb_read_unlock(&shadow->lock, s);
	if (err)
		return -err;
#else /* CONFIG_XENO_FASTSYNCH */
	if (err) {
		cb_read_unlock(&shadow->lock, s);
		return -err;
	}
#endif /* CONFIG_XENO_FASTSYNCH */

  checked:
	mutex = (pse51_mutex_t *) xnmalloc(sizeof(*mutex));
	if (!mutex)
		return ENOMEM;

#ifdef CONFIG_XENO_FASTSYNCH
	ownerp = (xnarch_atomic_t *)
		xnheap_alloc(&xnsys_ppd_get(attr->pshared)->sem_heap,
			     sizeof(xnarch_atomic_t));
	if (!ownerp) {
		xnfree(mutex);
		return EAGAIN;
	}
#endif /* CONFIG_XENO_FASTSYNCH */

	cb_force_write_lock(&shadow->lock, s);
	err = pse51_mutex_init_internal(shadow, mutex, ownerp, attr);
	cb_write_unlock(&shadow->lock, s);

	if (err) {
		xnfree(mutex);
#ifdef CONFIG_XENO_FASTSYNCH
		xnheap_free(&xnsys_ppd_get(attr->pshared)->sem_heap, ownerp);
#endif /* CONFIG_XENO_FASTSYNCH */
	}
	return -err;
}

void pse51_mutex_destroy_internal(pse51_mutex_t *mutex,
				  pse51_kqueues_t *q)
{
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	removeq(&q->mutexq, &mutex->link);
	/* synchbase wait queue may not be empty only when this function is called
	   from pse51_mutex_pkg_cleanup, hence the absence of xnpod_schedule(). */
	xnsynch_destroy(&mutex->synchbase);
	xnlock_put_irqrestore(&nklock, s);

#ifdef CONFIG_XENO_FASTSYNCH
	xnheap_free(&xnsys_ppd_get(mutex->attr.pshared)->sem_heap,
		    mutex->synchbase.fastlock);
#endif /* CONFIG_XENO_FASTSYNCH */
	xnfree(mutex);
}

/**
 * Destroy a mutex.
 *
 * This service destroys the mutex @a mx, if it is unlocked and not referenced
 * by any condition variable. The mutex becomes invalid for all mutex services
 * (they all return the EINVAL error) except pthread_mutex_init().
 *
 * @param mx the mutex to be destroyed.
 *
 * @return 0 on success,
 * @return an error number if:
 * - EINVAL, the mutex @a mx is invalid;
 * - EPERM, the mutex is not process-shared and does not belong to the current
 *   process;
 * - EBUSY, the mutex is locked, or used by a condition variable.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_mutex_destroy.html">
 * Specification.</a>
 *
 */
int pthread_mutex_destroy(pthread_mutex_t * mx)
{
	struct __shadow_mutex *shadow =
	    &((union __xeno_mutex *)mx)->shadow_mutex;
	DECLARE_CB_LOCK_FLAGS(s);
	pse51_mutex_t *mutex;

	if (unlikely(cb_try_write_lock(&shadow->lock, s)))
		return EBUSY;

	mutex = shadow->mutex;
	if (!pse51_obj_active(shadow, PSE51_MUTEX_MAGIC, struct __shadow_mutex)
	    || !pse51_obj_active(mutex, PSE51_MUTEX_MAGIC, struct pse51_mutex)) {
		cb_write_unlock(&shadow->lock, s);
		return EINVAL;
	}

	if (pse51_kqueues(mutex->attr.pshared) != mutex->owningq) {
		cb_write_unlock(&shadow->lock, s);
		return EPERM;
	}

#ifdef CONFIG_XENO_FASTSYNCH
	if (xnsynch_fast_owner_check(mutex->synchbase.fastlock,
				     XN_NO_HANDLE) != 0) {
#else /* CONFIG_XENO_FASTSYNCH */
	if (xnsynch_owner_check(&mutex->synchbase, NULL)) {
#endif
		cb_write_unlock(&shadow->lock, s);
		return EBUSY;
	}

	pse51_mark_deleted(shadow);
	pse51_mark_deleted(mutex);
	cb_write_unlock(&shadow->lock, s);

	pse51_mutex_destroy_internal(mutex, pse51_kqueues(mutex->attr.pshared));

	return 0;
}

int pse51_mutex_timedlock_break(struct __shadow_mutex *shadow,
				int timed, xnticks_t abs_to)
{
	xnthread_t *cur = xnpod_current_thread();
	pse51_mutex_t *mutex;
	spl_t s;
	int err;

	/* We need a valid thread handle for the fast lock. */
	if (xnthread_handle(cur) == XN_NO_HANDLE)
		return -EPERM;

	err = pse51_mutex_timedlock_internal(cur, shadow, 1, timed, abs_to);
	if (err != -EBUSY)
		goto unlock_and_return;

	mutex = shadow->mutex;

	switch(mutex->attr.type) {
	case PTHREAD_MUTEX_NORMAL:
		/* Attempting to relock a normal mutex, deadlock. */
		xnlock_get_irqsave(&nklock, s);
		for (;;) {
			if (timed)
				xnsynch_acquire(&mutex->synchbase,
						abs_to, XN_REALTIME);
			else
				xnsynch_acquire(&mutex->synchbase,
						XN_INFINITE, XN_RELATIVE);

			if (xnthread_test_info(cur, XNBREAK)) {
				err = -EINTR;
				break;
			}

			if (xnthread_test_info(cur, XNTIMEO)) {
				err = -ETIMEDOUT;
				break;
			}

			if (xnthread_test_info(cur, XNRMID)) {
				err = -EINVAL;
				break;
			}
		}
		xnlock_put_irqrestore(&nklock, s);

		break;

	case PTHREAD_MUTEX_ERRORCHECK:
		err = -EDEADLK;
		break;

	case PTHREAD_MUTEX_RECURSIVE:
		if (shadow->lockcnt == UINT_MAX) {
			err = -EAGAIN;
			break;
		}

		++shadow->lockcnt;
		err = 0;
	}

  unlock_and_return:
	return err;

}

/**
 * Attempt to lock a mutex.
 *
 * This service is equivalent to pthread_mutex_lock(), except that if the mutex
 * @a mx is locked by another thread than the current one, this service returns
 * immediately.
 *
 * @param mx the mutex to be locked.
 *
 * @return 0 on success;
 * @return an error number if:
 * - EPERM, the caller context is invalid;
 * - EINVAL, the mutex is invalid;
 * - EPERM, the mutex is not process-shared and does not belong to the current
 *   process;
 * - EBUSY, the mutex was locked by another thread than the current one;
 * - EAGAIN, the mutex is recursive, and the maximum number of recursive locks
 *   has been exceeded.
 *
 * @par Valid contexts:
 * - Xenomai kernel-space thread,
 * - Xenomai user-space thread (switches to primary mode).
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_mutex_trylock.html">
 * Specification.</a>
 *
 */
int pthread_mutex_trylock(pthread_mutex_t *mx)
{
	struct __shadow_mutex *shadow =
	    &((union __xeno_mutex *)mx)->shadow_mutex;
	xnthread_t *cur = xnpod_current_thread();
	pse51_mutex_t *mutex = shadow->mutex;
	DECLARE_CB_LOCK_FLAGS(s);
	int err;

	if (xnpod_unblockable_p())
		return EPERM;

	if (unlikely(cb_try_read_lock(&shadow->lock, s)))
		return EINVAL;

	if (!pse51_obj_active(shadow, PSE51_MUTEX_MAGIC,
			      struct __shadow_mutex)
	    || !pse51_obj_active(mutex, PSE51_MUTEX_MAGIC,
				 struct pse51_mutex)) {
		err = EINVAL;
		goto unlock_and_return;
	}

#if XENO_DEBUG(POSIX)
	if (mutex->owningq != pse51_kqueues(mutex->attr.pshared)) {
		err = EPERM;
		goto unlock_and_return;
	}
#endif /* XENO_DEBUG(POSIX) */

#ifdef CONFIG_XENO_FASTSYNCH
	err = -xnsynch_fast_acquire(mutex->synchbase.fastlock,
				    xnthread_handle(cur));
#else /* !CONFIG_XENO_FASTSYNCH */
	{
		xnthread_t *owner = xnsynch_owner(&mutex->synchbase);
		if (!owner)
			err = 0;
		else if (owner == cur)
			err = EBUSY;
		else
			err = EAGAIN;
	}
#endif /* !CONFIG_XENO_FASTSYNCH */

	if (likely(!err)) {
		if (xnthread_test_state(cur, XNOTHER) && !err)
			xnthread_inc_rescnt(cur);
		shadow->lockcnt = 1;
	}
	else if (err == EBUSY) {
		pse51_mutex_t *mutex = shadow->mutex;

		if (mutex->attr.type == PTHREAD_MUTEX_RECURSIVE) {
			if (shadow->lockcnt == UINT_MAX)
				err = EAGAIN;
			else {
				++shadow->lockcnt;
				err = 0;
			}
		}
	}

  unlock_and_return:
	cb_read_unlock(&shadow->lock, s);

	return err;
}

/**
 * Lock a mutex.
 *
 * This service attempts to lock the mutex @a mx. If the mutex is free, it
 * becomes locked. If it was locked by another thread than the current one, the
 * current thread is suspended until the mutex is unlocked. If it was already
 * locked by the current mutex, the behaviour of this service depends on the
 * mutex type :
 * - for mutexes of the @a PTHREAD_MUTEX_NORMAL type, this service deadlocks;
 * - for mutexes of the @a PTHREAD_MUTEX_ERRORCHECK type, this service returns
 *   the EDEADLK error number;
 * - for mutexes of the @a PTHREAD_MUTEX_RECURSIVE type, this service increments
 *   the lock recursion count and returns 0.
 *
 * @param mx the mutex to be locked.
 *
 * @return 0 on success
 * @return an error number if:
 * - EPERM, the caller context is invalid;
 * - EINVAL, the mutex @a mx is invalid;
 * - EPERM, the mutex is not process-shared and does not belong to the current
 *   process;
 * - EDEADLK, the mutex is of the @a PTHREAD_MUTEX_ERRORCHECK type and was
 *   already locked by the current thread;
 * - EAGAIN, the mutex is of the @a PTHREAD_MUTEX_RECURSIVE type and the maximum
 *   number of recursive locks has been exceeded.
 *
 * @par Valid contexts:
 * - Xenomai kernel-space thread;
 * - Xenomai user-space thread (switches to primary mode).
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_mutex_lock.html">
 * Specification.</a>
 *
 */
int pthread_mutex_lock(pthread_mutex_t * mx)
{
	struct __shadow_mutex *shadow =
	    &((union __xeno_mutex *)mx)->shadow_mutex;
	DECLARE_CB_LOCK_FLAGS(s);
	int err;

	if (unlikely(cb_try_read_lock(&shadow->lock, s)))
		return EINVAL;

	do {
		err = pse51_mutex_timedlock_break(shadow, 0, XN_INFINITE);
	} while (err == -EINTR);

	cb_read_unlock(&shadow->lock, s);

	return -err;
}

/**
 * Attempt, during a bounded time, to lock a mutex.
 *
 * This service is equivalent to pthread_mutex_lock(), except that if the mutex
 * @a mx is locked by another thread than the current one, this service only
 * suspends the current thread until the timeout specified by @a to expires.
 *
 * @param mx the mutex to be locked;
 *
 * @param to the timeout, expressed as an absolute value of the CLOCK_REALTIME
 * clock.
 *
 * @return 0 on success;
 * @return an error number if:
 * - EPERM, the caller context is invalid;
 * - EINVAL, the mutex @a mx is invalid;
 * - EPERM, the mutex is not process-shared and does not belong to the current
 *   process;
 * - ETIMEDOUT, the mutex could not be locked and the specified timeout
 *   expired;
 * - EDEADLK, the mutex is of the @a PTHREAD_MUTEX_ERRORCHECK type and the mutex
 *   was already locked by the current thread;
 * - EAGAIN, the mutex is of the @a PTHREAD_MUTEX_RECURSIVE type and the maximum
 *   number of recursive locks has been exceeded.
 *
 * @par Valid contexts:
 * - Xenomai kernel-space thread;
 * - Xenomai user-space thread (switches to primary mode).
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_mutex_timedlock.html">
 * Specification.</a>
 *
 */
int pthread_mutex_timedlock(pthread_mutex_t * mx, const struct timespec *to)
{
	struct __shadow_mutex *shadow =
	    &((union __xeno_mutex *)mx)->shadow_mutex;
	DECLARE_CB_LOCK_FLAGS(s);
	int err;

	if (unlikely(cb_try_read_lock(&shadow->lock, s)))
		return EINVAL;

	do {
		err = pse51_mutex_timedlock_break(shadow, 1,
						  ts2ticks_ceil(to) + 1);
	} while (err == -EINTR);

	cb_read_unlock(&shadow->lock, s);

	return -err;
}

/**
 * Unlock a mutex.
 *
 * This service unlocks the mutex @a mx. If the mutex is of the @a
 * PTHREAD_MUTEX_RECURSIVE @a type and the locking recursion count is greater
 * than one, the lock recursion count is decremented and the mutex remains
 * locked.
 *
 * Attempting to unlock a mutex which is not locked or which is locked by
 * another thread than the current one yields the EPERM error, whatever the
 * mutex @a type attribute.
 *
 * @param mx the mutex to be released.
 *
 * @return 0 on success;
 * @return an error number if:
 * - EPERM, the caller context is invalid;
 * - EINVAL, the mutex @a mx is invalid;
 * - EPERM, the mutex was not locked by the current thread.
 *
 * @par Valid contexts:
 * - Xenomai kernel-space thread,
 * - kernel-space cancellation cleanup routine,
 * - Xenomai user-space thread (switches to primary mode),
 * - user-space cancellation cleanup routine.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_mutex_unlock.html">
 * Specification.</a>
 *
 */
int pthread_mutex_unlock(pthread_mutex_t * mx)
{
	struct __shadow_mutex *shadow =
	    &((union __xeno_mutex *)mx)->shadow_mutex;
	xnthread_t *cur = xnpod_current_thread();
	DECLARE_CB_LOCK_FLAGS(s);
	pse51_mutex_t *mutex;
	int err;

	if (xnpod_root_p() || xnpod_interrupt_p())
		return EPERM;

	if (unlikely(cb_try_read_lock(&shadow->lock, s)))
		return EINVAL;

	mutex = shadow->mutex;

	if (!pse51_obj_active(shadow,
			      PSE51_MUTEX_MAGIC, struct __shadow_mutex)
	    || !pse51_obj_active(mutex, PSE51_MUTEX_MAGIC, struct pse51_mutex)) {
		err = EINVAL;
		goto out;
	}

	err = -xnsynch_owner_check(&mutex->synchbase, cur);
	if (err)
		goto out;

	if (shadow->lockcnt > 1) {
		/* Mutex is recursive */
		--shadow->lockcnt;
		cb_read_unlock(&shadow->lock, s);
		return 0;
	}

	if (xnsynch_release(&mutex->synchbase))
		xnpod_schedule();

  out:
	cb_read_unlock(&shadow->lock, s);

	return err;
}

void pse51_mutexq_cleanup(pse51_kqueues_t *q)
{
	xnholder_t *holder;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	while ((holder = getheadq(&q->mutexq)) != NULL) {
		xnlock_put_irqrestore(&nklock, s);
		pse51_mutex_destroy_internal(link2mutex(holder), q);
#if XENO_DEBUG(POSIX)
		xnprintf("Posix: destroying mutex %p.\n", link2mutex(holder));
#endif /* XENO_DEBUG(POSIX) */
		xnlock_get_irqsave(&nklock, s);
	}

	xnlock_put_irqrestore(&nklock, s);
}

void pse51_mutex_pkg_init(void)
{
	initq(&pse51_global_kqueues.mutexq);
	pthread_mutexattr_init(&pse51_default_mutex_attr);
}

void pse51_mutex_pkg_cleanup(void)
{
	pse51_mutexq_cleanup(&pse51_global_kqueues);
}

/*@}*/

EXPORT_SYMBOL_GPL(pthread_mutex_init);
EXPORT_SYMBOL_GPL(pthread_mutex_destroy);
EXPORT_SYMBOL_GPL(pthread_mutex_trylock);
EXPORT_SYMBOL_GPL(pthread_mutex_lock);
EXPORT_SYMBOL_GPL(pthread_mutex_timedlock);
EXPORT_SYMBOL_GPL(pthread_mutex_unlock);

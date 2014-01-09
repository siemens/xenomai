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
 * @ingroup cobalt
 * @defgroup cobalt_sem Semaphores services.
 *
 * Semaphores services.
 *
 * Semaphores are counters for resources shared between threads. The basic
 * operations on semaphores are: increment the counter atomically, and wait
 * until the counter is non-null and decrement it atomically.
 *
 * Semaphores have a maximum value past which they cannot be incremented.  The
 * macro @a SEM_VALUE_MAX is defined to be this maximum value.
 *
 *@{*/

#include <stddef.h>
#include <linux/err.h>
#include "internal.h"
#include "thread.h"
#include "clock.h"
#include "sem.h"

#define SEM_NAMED    0x80000000

static inline struct cobalt_kqueues *sem_kqueue(struct cobalt_sem *sem)
{
	int pshared = !!(sem->flags & SEM_PSHARED);
	return cobalt_kqueues(pshared);
}

int cobalt_sem_destroy_inner(xnhandle_t handle)
{
	struct cobalt_sem *sem;
	int ret = 0;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	sem = xnregistry_fetch(handle);
	if (!cobalt_obj_active(sem, COBALT_SEM_MAGIC, typeof(*sem))) {
		ret = -EINVAL;
		goto unlock_error;
	}
	if (--sem->refs) {
		ret = -EBUSY;
	  unlock_error:
		xnlock_put_irqrestore(&nklock, s);
		return ret;
	}

	cobalt_mark_deleted(sem);
	list_del(&sem->link);
	if (xnsynch_destroy(&sem->synchbase) == XNSYNCH_RESCHED) {
		xnsched_run();
		ret = 1;
	}
	
	xnlock_put_irqrestore(&nklock, s);

	xnheap_free(&xnsys_ppd_get(!!(sem->flags & SEM_PSHARED))->sem_heap,
		sem->datp);
	xnregistry_remove(sem->handle);
	
	xnfree(sem);

	return ret;
}

struct cobalt_sem *
cobalt_sem_init_inner(const char *name, struct __shadow_sem *sm, 
		      int flags, unsigned int value)
{
	struct list_head *semq;
	struct cobalt_sem *sem, *osem;
	struct cobalt_kqueues *kq;
	struct xnsys_ppd *sys_ppd;
	struct sem_dat *datp;
	int ret, sflags;
	spl_t s;

	if ((flags & SEM_PULSE) != 0 && value > 0)
		return ERR_PTR(-EINVAL);

	sem = xnmalloc(sizeof(*sem));
	if (sem == NULL)
		return ERR_PTR(-ENOSPC);

	snprintf(sem->name, sizeof(sem->name), "%s", name);

	sys_ppd = xnsys_ppd_get(!!(flags & SEM_PSHARED));
	datp = xnheap_alloc(&sys_ppd->sem_heap, sizeof(*datp));
	if (datp == NULL) {
		ret = -EAGAIN;
		goto err_free_sem;
	}

	xnlock_get_irqsave(&nklock, s);

	if (sm->magic != COBALT_SEM_MAGIC &&
	    sm->magic != COBALT_NAMED_SEM_MAGIC)
		goto do_init;

	semq = &cobalt_kqueues(!!(flags & SEM_PSHARED))->semq;
	if (list_empty(semq))
		goto do_init;

	/*
	 * Make sure we are not reinitializing a valid semaphore. As a
	 * special exception, we allow reinitializing a shared
	 * anonymous semaphore. Rationale: if the process creating
	 * such semaphore exits, we may assume that other processes
	 * sharing that semaphore won't be able to keep on running.
	 */
	osem = xnregistry_fetch(sm->handle);
	if (!cobalt_obj_active(osem, COBALT_SEM_MAGIC, typeof(*osem)))
		goto do_init;

	if ((flags & SEM_PSHARED) == 0 || sm->magic != COBALT_SEM_MAGIC) {
		ret = -EBUSY;
		goto err_lock_put;
	}

	xnlock_put_irqrestore(&nklock, s);
	cobalt_sem_destroy_inner(sm->handle);
	xnlock_get_irqsave(&nklock, s);
  do_init:
	if (value > (unsigned)SEM_VALUE_MAX) {
		ret = -EINVAL;
		goto err_lock_put;
	}
	
	ret = xnregistry_enter(sem->name, sem, &sem->handle, NULL);
	if (ret < 0)
		goto err_lock_put;

	sem->magic = COBALT_SEM_MAGIC;
	kq = cobalt_kqueues(!!(flags & SEM_PSHARED));
	list_add_tail(&sem->link, &kq->semq);
	sflags = flags & SEM_FIFO ? 0 : XNSYNCH_PRIO;
	xnsynch_init(&sem->synchbase, sflags, NULL);

	sem->datp = datp;
	atomic_long_set(&datp->value, value);
	datp->flags = flags;
	sem->flags = flags;
	sem->owningq = kq;
	sem->refs = name[0] ? 2 : 1;
			
	sm->magic = name[0] ? COBALT_NAMED_SEM_MAGIC : COBALT_SEM_MAGIC;
	sm->handle = sem->handle;
	sm->datp_offset = xnheap_mapped_offset(&sys_ppd->sem_heap, datp);
	if (flags & SEM_PSHARED)
		sm->datp_offset = -sm->datp_offset;
	xnlock_put_irqrestore(&nklock, s);

	return sem;

  err_lock_put:
	xnlock_put_irqrestore(&nklock, s);
	xnheap_free(&sys_ppd->sem_heap, sem->datp);
  err_free_sem:
	xnfree(sem);

	return ERR_PTR(ret);
}

/**
 * Destroy an unnamed semaphore.
 *
 * This service destroys the semaphore @a sm. Threads currently blocked on @a sm
 * are unblocked and the service they called return -1 with @a errno set to
 * EINVAL. The semaphore is then considered invalid by all semaphore services
 * (they all fail with @a errno set to EINVAL) except sem_init().
 *
 * This service fails if @a sm is a named semaphore.
 *
 * @param sm the semaphore to be destroyed.
 *
 * @retval always 0 on success.  If SEM_WARNDEL was mentioned in
 * sem_init_np(), the semaphore is deleted as requested and a strictly
 * positive value is returned to warn the caller if threads were
 * pending on it, otherwise zero is returned. If SEM_NOBUSYDEL was
 * mentioned in sem_init_np(), sem_destroy() may succeed only if no
 * thread is waiting on the semaphore to delete, otherwise -EBUSY is
 * returned.
 *
 * @retval -1 with @a errno set if:
 * - EINVAL, the semaphore @a sm is invalid or a named semaphore;
 * - EPERM, the semaphore @a sm is not process-shared and does not belong to the
 *   current process.
 * - EBUSY, a thread is currently waiting on the semaphore @a sm with
 * SEM_NOBUSYDEL set.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sem_destroy.html">
 * Specification.</a>
 *
 */
static int sem_destroy(struct __shadow_sem *sm)
{
	struct cobalt_sem *sem;
	int warn, ret;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	if (sm->magic != COBALT_SEM_MAGIC) {
		ret = -EINVAL;
		goto error;
	}

	sem = xnregistry_fetch(sm->handle);
	if (!cobalt_obj_active(sem, COBALT_SEM_MAGIC, typeof(*sem))) {
		ret = -EINVAL;
		goto error;
	}

	if (sem_kqueue(sem) != sem->owningq) {
		ret = -EPERM;
		goto error;
	}

	if ((sem->flags & SEM_NOBUSYDEL) != 0 &&
	    xnsynch_pended_p(&sem->synchbase)) {
		ret = -EBUSY;
		goto error;
	}

	warn = sem->flags & SEM_WARNDEL;
	cobalt_mark_deleted(sm);
	xnlock_put_irqrestore(&nklock, s);

	ret = cobalt_sem_destroy_inner(sem->handle);

	return warn ? ret : 0;

      error:

	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

/**
 * Open a named semaphore.
 *
 * This service establishes a connection between the semaphore named @a name and
 * the calling context (kernel-space as a whole, or user-space process).
 *
 * If no semaphore named @a name exists and @a oflags has the @a O_CREAT bit
 * set, the semaphore is created by this function, using two more arguments:
 * - a @a mode argument, of type @b mode_t, currently ignored;
 * - a @a value argument, of type @b unsigned, specifying the initial value of
 *   the created semaphore.
 *
 * If @a oflags has the two bits @a O_CREAT and @a O_EXCL set and the semaphore
 * already exists, this service fails.
 *
 * @a name may be any arbitrary string, in which slashes have no particular
 * meaning. However, for portability, using a name which starts with a slash and
 * contains no other slash is recommended.
 *
 * If sem_open() is called from the same context (kernel-space as a whole, or
 * user-space process) several times with the same value of @a name, the same
 * address is returned.
 *
 * @param name the name of the semaphore to be created;
 *
 * @param oflags flags.
 *
 * @return the address of the named semaphore on success;
 * @return SEM_FAILED with @a errno set if:
 * - ENAMETOOLONG, the length of the @a name argument exceeds 64 characters;
 * - EEXIST, the bits @a O_CREAT and @a O_EXCL were set in @a oflags and the
 *   named semaphore already exists;
 * - ENOENT, the bit @a O_CREAT is not set in @a oflags and the named semaphore
 *   does not exist;
 * - ENOSPC, insufficient memory exists in the system heap to create the
 *   semaphore, increase CONFIG_XENO_OPT_SYS_HEAPSZ;
 * - EINVAL, the @a value argument exceeds @a SEM_VALUE_MAX.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sem_open.html">
 * Specification.</a>
 *
 */

/**
 * Close a named semaphore.
 *
 * This service closes the semaphore @a sm. The semaphore is destroyed only when
 * unlinked with a call to the sem_unlink() service and when each call to
 * sem_open() matches a call to this service.
 *
 * When a semaphore is destroyed, the memory it used is returned to the system
 * heap, so that further references to this semaphore are not guaranteed to
 * fail, as is the case for unnamed semaphores.
 *
 * This service fails if @a sm is an unnamed semaphore.
 *
 * @param sm the semaphore to be closed.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, the semaphore @a sm is invalid or is an unnamed semaphore.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sem_close.html">
 * Specification.</a>
 *
 */

/**
 * Unlink a named semaphore.
 *
 * This service unlinks the semaphore named @a name. This semaphore is not
 * destroyed until all references obtained with sem_open() are closed by calling
 * sem_close(). However, the unlinked semaphore may no longer be reached with
 * the sem_open() service.
 *
 * When a semaphore is destroyed, the memory it used is returned to the system
 * heap, so that further references to this semaphore are not guaranteed to
 * fail, as is the case for unnamed semaphores.
 *
 * @param name the name of the semaphore to be unlinked.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - ENAMETOOLONG, the length of the @a name argument exceeds 64 characters;
 * - ENOENT, the named semaphore does not exist.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sem_unlink.html">
 * Specification.</a>
 *
 */

static inline int sem_trywait_inner(struct cobalt_sem *sem)
{
	if (sem == NULL || sem->magic != COBALT_SEM_MAGIC)
		return -EINVAL;

#if XENO_DEBUG(COBALT)
	if (sem->owningq != sem_kqueue(sem))
		return -EPERM;
#endif /* XENO_DEBUG(COBALT) */

	if (atomic_long_sub_return(1, &sem->datp->value) < 0)
		return -EAGAIN;

	return 0;
}

/**
 * Attempt to decrement a semaphore.
 *
 * This service is equivalent to sem_wait(), except that it returns
 * immediately if the semaphore @a sm is currently depleted, and that
 * it is not a cancellation point.
 *
 * @param sem the semaphore to be decremented.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, the specified semaphore is invalid or uninitialized;
 * - EPERM, the semaphore @a sm is not process-shared and does not belong to the
 *   current process;
 * - EAGAIN, the specified semaphore is currently fully depleted.
 *
 * * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sem_trywait.html">
 * Specification.</a>
 *
 */
static int sem_trywait(xnhandle_t handle)
{
	int err;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	err = sem_trywait_inner(xnregistry_fetch(handle));
	xnlock_put_irqrestore(&nklock, s);

	return err;
}

static inline int
sem_wait_inner(xnhandle_t handle, int timed,
	       const struct timespec __user *u_ts)
{
	struct cobalt_sem *sem;
	struct timespec ts;
	xntmode_t tmode;
	int ret, info;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	sem = xnregistry_fetch(handle);

	ret = sem_trywait_inner(sem);
	if (ret != -EAGAIN) {
		xnlock_put_irqrestore(&nklock, s);
		return ret;
	}

	if (timed) {
		if (u_ts == NULL ||
			__xn_safe_copy_from_user(&ts, u_ts, sizeof(ts))) {
			ret = -EFAULT;
			goto fail;
		}

		if (ts.tv_nsec >= ONE_BILLION) {
			ret = -EINVAL;
			goto fail;
		}

		tmode = sem->flags & SEM_RAWCLOCK ? XN_ABSOLUTE : XN_REALTIME;
		info = xnsynch_sleep_on(&sem->synchbase, ts2ns(&ts) + 1, tmode);
	} else
		info = xnsynch_sleep_on(&sem->synchbase, 
					XN_INFINITE, XN_RELATIVE);
	if (info & XNRMID) {
		ret = -EINVAL;
		goto out;
	}

	ret = 0;
	if (info & (XNBREAK|XNTIMEO))
		ret = (info & XNBREAK) ? -EINTR : -ETIMEDOUT;
fail:
	atomic_long_inc(&sem->datp->value);
out:
	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

/**
 * Decrement a semaphore.
 *
 * This service decrements the semaphore @a sm if it is currently if
 * its value is greater than 0. If the semaphore's value is currently
 * zero, the calling thread is suspended until the semaphore is
 * posted, or a signal is delivered to the calling thread.
 *
 * This service is a cancellation point for Cobalt threads (created
 * with the pthread_create() service). When such a thread is cancelled
 * while blocked in a call to this service, the semaphore state is
 * left unchanged before the cancellation cleanup handlers are called.
 *
 * @param sem the semaphore to be decremented.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EPERM, the caller context is invalid;
 * - EINVAL, the semaphore is invalid or uninitialized;
 * - EPERM, the semaphore @a sm is not process-shared and does not belong to the
 *   current process;
 * - EINTR, the caller was interrupted by a signal while blocked in this
 *   service.
 *
 * @par Valid contexts:
 * - Xenomai kernel-space thread,
 * - Xenomai user-space thread (switches to primary mode).
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sem_wait.html">
 * Specification.</a>
 *
 */
static int sem_wait(xnhandle_t handle)
{
	return sem_wait_inner(handle, 0, NULL);
}

/**
 * Attempt, during a bounded time, to decrement a semaphore.
 *
 * This service is equivalent to sem_wait(), except that the caller is only
 * blocked until the timeout @a abs_timeout expires.
 *
 * @param sem the semaphore to be decremented;
 *
 * @param abs_timeout the timeout, expressed as an absolute value of
 * the relevant clock for the semaphore, either CLOCK_MONOTONIC if
 * SEM_RAWCLOCK was mentioned via sem_init_np(), or CLOCK_REALTIME
 * otherwise.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EPERM, the caller context is invalid;
 * - EINVAL, the semaphore is invalid or uninitialized;
 * - EINVAL, the specified timeout is invalid;
 * - EPERM, the semaphore @a sm is not process-shared and does not belong to the
 *   current process;
 * - EINTR, the caller was interrupted by a signal while blocked in this
 *   service;
 * - ETIMEDOUT, the semaphore could not be decremented and the
 *   specified timeout expired.
 *
 * @par Valid contexts:
 * - Xenomai kernel-space thread,
 * - Xenomai user-space thread (switches to primary mode).
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sem_timedwait.html">
 * Specification.</a>
 *
 */
static int sem_timedwait(xnhandle_t handle,
			 const struct timespec __user *abs_timeout)
{
	return sem_wait_inner(handle, 1, abs_timeout);
}

int sem_post_inner(struct cobalt_sem *sem, struct cobalt_kqueues *ownq, int bcast)
{
	if (sem == NULL || sem->magic != COBALT_SEM_MAGIC)
		return -EINVAL;

#if XENO_DEBUG(COBALT)
	if (ownq && ownq != sem_kqueue(sem))
		return -EPERM;
#endif /* XENO_DEBUG(COBALT) */

	if (atomic_long_read(&sem->datp->value) == SEM_VALUE_MAX)
		return -EINVAL;

	if (!bcast) {
		if (atomic_long_inc_return(&sem->datp->value) <= 0) {
			if (xnsynch_wakeup_one_sleeper(&sem->synchbase))
				xnsched_run();
		} else if (sem->flags & SEM_PULSE)
			atomic_long_set(&sem->datp->value, 0);
	} else {
		if (atomic_long_read(&sem->datp->value) < 0) {
			atomic_long_set(&sem->datp->value, 0);
			if (xnsynch_flush(&sem->synchbase, 0) == 
				XNSYNCH_RESCHED)
				xnsched_run();
		}
	}

	return 0;
}

/**
 * Post a semaphore.
 *
 * This service posts the semaphore @a sm.
 *
 * If no thread is currently blocked on this semaphore, its count is
 * incremented unless "pulse" mode is enabled for it (see
 * sem_init_np(), SEM_PULSE). If a thread is blocked on the semaphore,
 * the thread heading the wait queue is unblocked.
 *
 * @param sm the semaphore to be signaled.
 *
 * @retval 0 on success;
 * @retval -1 with errno set if:
 * - EINVAL, the specified semaphore is invalid or uninitialized;
 * - EPERM, the semaphore @a sm is not process-shared and does not belong to the
 *   current process;
 * - EAGAIN, the semaphore count is @a SEM_VALUE_MAX.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sem_post.html">
 * Specification.</a>
 *
 */
static int sem_post(xnhandle_t handle)
{
	struct cobalt_sem *sm;
	int ret;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	sm = xnregistry_fetch(handle);
	ret = sem_post_inner(sm, sm->owningq, 0);
	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

/**
 * Get the value of a semaphore.
 *
 * This service stores at the address @a value, the current count of the
 * semaphore @a sm. The state of the semaphore is unchanged.
 *
 * If the semaphore is currently fully depleted, the value stored is
 * zero, unless SEM_REPORT was mentioned for a non-standard semaphore
 * (see sem_init_np()), in which case the current number of waiters is
 * returned as the semaphore's negative value (e.g. -2 would mean the
 * semaphore is fully depleted AND two threads are currently pending
 * on it).
 *
 * @param sem a semaphore;
 *
 * @param value address where the semaphore count will be stored on success.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, the semaphore is invalid or uninitialized;
 * - EPERM, the semaphore @a sm is not process-shared and does not belong to the
 *   current process.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sem_getvalue.html">
 * Specification.</a>
 *
 */
static int sem_getvalue(xnhandle_t handle, int *value)
{
	struct cobalt_sem *sem;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	sem = xnregistry_fetch(handle);

	if (sem == NULL || sem->magic != COBALT_SEM_MAGIC) {
		xnlock_put_irqrestore(&nklock, s);
		return -EINVAL;
	}

	if (sem->owningq != sem_kqueue(sem)) {
		xnlock_put_irqrestore(&nklock, s);
		return -EPERM;
	}

	*value = atomic_long_read(&sem->datp->value);
	if ((sem->flags & SEM_REPORT) == 0 && *value < 0)
		*value = 0;

	xnlock_put_irqrestore(&nklock, s);

	return 0;
}

int cobalt_sem_init(struct __shadow_sem __user *u_sem, int pshared, unsigned value)
{
	struct __shadow_sem sm;
	struct cobalt_sem *sem;

	if (__xn_safe_copy_from_user(&sm, u_sem, sizeof(sm)))
		return -EFAULT;

	sem = cobalt_sem_init_inner("", &sm, pshared ? SEM_PSHARED : 0, value);
	if (IS_ERR(sem))
		return PTR_ERR(sem);

	return __xn_safe_copy_to_user(u_sem, &sm, sizeof(*u_sem));
}

int cobalt_sem_post(struct __shadow_sem __user *u_sem)
{
	xnhandle_t handle;

	__xn_get_user(handle, &u_sem->handle);

	return sem_post(handle);
}

int cobalt_sem_wait(struct __shadow_sem __user *u_sem)
{
	xnhandle_t handle;

	__xn_get_user(handle, &u_sem->handle);

	return sem_wait(handle);
}

int cobalt_sem_timedwait(struct __shadow_sem __user *u_sem,
			 struct timespec __user *u_ts)
{
	xnhandle_t handle;

	__xn_get_user(handle, &u_sem->handle);

	return sem_timedwait(handle, u_ts);
}

int cobalt_sem_trywait(struct __shadow_sem __user *u_sem)
{
	xnhandle_t handle;

	__xn_get_user(handle, &u_sem->handle);

	return sem_trywait(handle);
}

int cobalt_sem_getvalue(struct __shadow_sem __user *u_sem, int __user *u_sval)
{
	xnhandle_t handle;
	int err, sval;

	__xn_get_user(handle, &u_sem->handle);

	err = sem_getvalue(handle, &sval);
	if (err < 0)
		return err;

	return __xn_safe_copy_to_user(u_sval, &sval, sizeof(sval));
}

int cobalt_sem_destroy(struct __shadow_sem __user *u_sem)
{
	struct __shadow_sem sm;
	int err;

	if (__xn_safe_copy_from_user(&sm, u_sem, sizeof(sm)))
		return -EFAULT;

	err = sem_destroy(&sm);
	if (err < 0)
		return err;

	return __xn_safe_copy_to_user(u_sem, &sm, sizeof(*u_sem)) ?: err;
}

int cobalt_sem_init_np(struct __shadow_sem __user *u_sem,
		       int flags, unsigned value)
{
	struct __shadow_sem sm;
	struct cobalt_sem *sem;

	if (__xn_safe_copy_from_user(&sm, u_sem, sizeof(sm)))
		return -EFAULT;

	if (flags & ~(SEM_FIFO|SEM_PULSE|SEM_PSHARED|SEM_REPORT|\
		      SEM_WARNDEL|SEM_RAWCLOCK|SEM_NOBUSYDEL))
		return -EINVAL;

	sem = cobalt_sem_init_inner("", &sm, flags, value);
	if (IS_ERR(sem))
		return PTR_ERR(sem);

	return __xn_safe_copy_to_user(u_sem, &sm, sizeof(*u_sem));
}

int cobalt_sem_broadcast_np(struct __shadow_sem __user *u_sem)
{
	struct cobalt_sem *sm;
	xnhandle_t handle;
	spl_t s;
	int err;

	__xn_get_user(handle, &u_sem->handle);

	xnlock_get_irqsave(&nklock, s);
	sm = xnregistry_fetch(handle);
	err = sem_post_inner(sm, sm->owningq, 1);
	xnlock_put_irqrestore(&nklock, s);

	return err;
}

void cobalt_semq_cleanup(struct cobalt_kqueues *q)
{
	struct cobalt_sem *sem, *tmp;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (list_empty(&q->semq))
		goto out;

	list_for_each_entry_safe(sem, tmp, &q->semq, link) {
		xnlock_put_irqrestore(&nklock, s);
#if XENO_DEBUG(COBALT)
		if (sem->flags & SEM_NAMED)
			printk(XENO_INFO "unlinking Cobalt semaphore \"%s\"\n",
				xnregistry_key(sem->handle));
		 else
			printk(XENO_INFO "deleting Cobalt semaphore %p\n", sem);
#endif /* XENO_DEBUG(COBALT) */
		if (sem->flags & SEM_NAMED)
			cobalt_nsem_unlink_inner(sem->handle);
		cobalt_sem_destroy_inner(sem->handle);
		xnlock_get_irqsave(&nklock, s);
	}
out:
	xnlock_put_irqrestore(&nklock, s);
}


void cobalt_sem_pkg_init(void)
{
	INIT_LIST_HEAD(&cobalt_global_kqueues.semq);
}

void cobalt_sem_pkg_cleanup(void)
{
	cobalt_semq_cleanup(&cobalt_global_kqueues);
}

/*@}*/

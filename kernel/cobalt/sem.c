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
#include <stdarg.h>
#include "registry.h"	/* For named semaphores. */
#include "thread.h"
#include "sem.h"

#define SEM_NAMED    0x80000000

typedef struct cobalt_sem {
	unsigned int magic;
	xnsynch_t synchbase;
	xnholder_t link;	/* Link in semq */
	unsigned int value;
	int flags;
	cobalt_kqueues_t *owningq;
} cobalt_sem_t;

static inline cobalt_kqueues_t *sem_kqueue(struct cobalt_sem *sem)
{
	int pshared = !!(sem->flags & SEM_PSHARED);
	return cobalt_kqueues(pshared);
}

#define link2sem(laddr) container_of(laddr, cobalt_sem_t, link)

typedef struct cobalt_named_sem {
	cobalt_sem_t sembase;	/* Has to be the first member. */
	cobalt_node_t nodebase;
	union __xeno_sem descriptor;
} nsem_t;

#define sem2named_sem(saddr) ((nsem_t *)(saddr))
#define node2sem(naddr) container_of(naddr, nsem_t, nodebase)

typedef struct cobalt_uptr {
	struct mm_struct *mm;
	unsigned refcnt;
	unsigned long uaddr;
	xnholder_t link;
} cobalt_uptr_t;

#define link2uptr(laddr) container_of(laddr, cobalt_uptr_t, link)

static int sem_destroy_inner(cobalt_sem_t *sem, cobalt_kqueues_t *q)
{
	int ret = 0;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	removeq(&q->semq, &sem->link);
	if (xnsynch_destroy(&sem->synchbase) == XNSYNCH_RESCHED) {
		xnpod_schedule();
		ret = 1;
	}
	xnlock_put_irqrestore(&nklock, s);

	if (sem->flags & SEM_NAMED)
		xnfree(sem2named_sem(sem));
	else
		xnfree(sem);

	return ret;
}

/* Called with nklock locked, irq off. */
static int sem_init_inner(cobalt_sem_t *sem, int flags, unsigned int value)
{
	int pshared, sflags;

	if (value > (unsigned)SEM_VALUE_MAX)
		return -EINVAL;

	pshared = !!(flags & SEM_PSHARED);
	sflags = flags & SEM_FIFO ? 0 : XNSYNCH_PRIO;

	sem->magic = COBALT_SEM_MAGIC;
	inith(&sem->link);
	appendq(&cobalt_kqueues(pshared)->semq, &sem->link);
	xnsynch_init(&sem->synchbase, sflags, NULL);
	sem->value = value;
	sem->flags = flags;
	sem->owningq = cobalt_kqueues(pshared);

	return 0;
}

static int do_sem_init(struct __shadow_sem *sm, int flags, unsigned int value)
{
	xnholder_t *holder;
	cobalt_sem_t *sem;
	xnqueue_t *semq;
	int ret;
	spl_t s;

	if ((flags & SEM_PULSE) != 0 && value > 0)
		return -EINVAL;

	sem = (cobalt_sem_t *)xnmalloc(sizeof(*sem));
	if (sem == NULL)
		return -ENOSPC;

	xnlock_get_irqsave(&nklock, s);

	semq = &cobalt_kqueues(!!(flags & SEM_PSHARED))->semq;

	if (sm->magic == COBALT_SEM_MAGIC
	    || sm->magic == COBALT_NAMED_SEM_MAGIC
	    || sm->magic == ~COBALT_NAMED_SEM_MAGIC) {
		for (holder = getheadq(semq); holder;
		     holder = nextq(semq, holder))
			if (holder == &sm->sem->link) {
				ret = -EBUSY;
				goto err_lock_put;
			}
	}

	ret = sem_init_inner(sem, flags, value);
	if (ret)
		goto err_lock_put;

	sm->magic = COBALT_SEM_MAGIC;
	sm->sem = sem;
	xnlock_put_irqrestore(&nklock, s);

	return 0;

  err_lock_put:
	xnlock_put_irqrestore(&nklock, s);
	xnfree(sem);

	return ret;
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
 * @retval always 0 on success if SEM_WARNDEL was not mentioned via
 * sem_init_np().  If SEM_WARNDEL was mentioned, then a strictly
 * positive value is returned to warn the caller if threads were
 * pending on the semaphore, or zero otherwise.
 *
 * @retval -1 with @a errno set if:
 * - EINVAL, the semaphore @a sm is invalid or a named semaphore;
 * - EPERM, the semaphore @a sm is not process-shared and does not belong to the
 *   current process.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sem_destroy.html">
 * Specification.</a>
 *
 */
static int sem_destroy(struct __shadow_sem *sm)
{
	cobalt_sem_t *sem;
	int warn, ret;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	if (sm->magic != COBALT_SEM_MAGIC
	    || sm->sem->magic != COBALT_SEM_MAGIC) {
		ret = -EINVAL;
		goto error;
	}

	sem = sm->sem;
	if (sem_kqueue(sem) != sem->owningq) {
		ret = -EPERM;
		goto error;
	}

	warn = sem->flags & SEM_WARNDEL;
	cobalt_mark_deleted(sm);
	cobalt_mark_deleted(sem);
	xnlock_put_irqrestore(&nklock, s);

	ret = sem_destroy_inner(sem, sem_kqueue(sem));

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
sem_t *sem_open(const char *name, int oflags, ...)
{
	cobalt_node_t *node;
	nsem_t *named_sem;
	unsigned value;
	mode_t mode;
	va_list ap;
	spl_t s;
	int err;

	xnlock_get_irqsave(&nklock, s);
	err = -cobalt_node_get(&node, name, COBALT_NAMED_SEM_MAGIC, oflags);
	xnlock_put_irqrestore(&nklock, s);

	if (err)
		goto error;

	if (node) {
		named_sem = node2sem(node);
		goto got_sem;
	}

	named_sem = (nsem_t *)xnmalloc(sizeof(*named_sem));
	if (!named_sem) {
		err = -ENOSPC;
		goto error;
	}
	named_sem->descriptor.shadow_sem.sem = &named_sem->sembase;

	va_start(ap, oflags);
	mode = va_arg(ap, int);	/* unused */
	value = va_arg(ap, unsigned);
	va_end(ap);

	xnlock_get_irqsave(&nklock, s);
	err = sem_init_inner(&named_sem->sembase, SEM_PSHARED|SEM_NAMED, value);
	if (err) {
		xnlock_put_irqrestore(&nklock, s);
		xnfree(named_sem);
		goto error;
	}

	err = -cobalt_node_add(&named_sem->nodebase, name, COBALT_NAMED_SEM_MAGIC);
	if (err && err != -EEXIST)
		goto err_put_lock;

	if (err == -EEXIST) {
		err = -cobalt_node_get(&node, name, COBALT_NAMED_SEM_MAGIC, oflags);
		if (err)
			goto err_put_lock;

		xnlock_put_irqrestore(&nklock, s);
		sem_destroy_inner(&named_sem->sembase, sem_kqueue(&named_sem->sembase));
		named_sem = node2sem(node);
		goto got_sem;
	}
	xnlock_put_irqrestore(&nklock, s);

  got_sem:
	/* Set the magic, needed both at creation and when re-opening a semaphore
	   that was closed but not unlinked. */
	named_sem->descriptor.shadow_sem.magic = COBALT_NAMED_SEM_MAGIC;

	return &named_sem->descriptor.native_sem;

  err_put_lock:
	xnlock_put_irqrestore(&nklock, s);
	sem_destroy_inner(&named_sem->sembase, sem_kqueue(&named_sem->sembase));
  error:
	return (sem_t *)ERR_PTR(err);
}

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
int sem_close(struct __shadow_sem *sm)
{
	nsem_t *named_sem;
	spl_t s;
	int err;

	xnlock_get_irqsave(&nklock, s);

	if (sm->magic != COBALT_NAMED_SEM_MAGIC
	    || sm->sem->magic != COBALT_SEM_MAGIC) {
		err = EINVAL;
		goto error;
	}

	named_sem = sem2named_sem(sm->sem);

	err = cobalt_node_put(&named_sem->nodebase);

	if (err)
		goto error;

	if (cobalt_node_removed_p(&named_sem->nodebase)) {
		/* unlink was called, and this semaphore is no longer referenced. */
		cobalt_mark_deleted(sm);
		cobalt_mark_deleted(&named_sem->sembase);
		xnlock_put_irqrestore(&nklock, s);

		sem_destroy_inner(&named_sem->sembase, cobalt_kqueues(1));
	} else if (!cobalt_node_ref_p(&named_sem->nodebase)) {
		/* this semaphore is no longer referenced, but not unlinked. */
		cobalt_mark_deleted(sm);
		xnlock_put_irqrestore(&nklock, s);
	} else
		xnlock_put_irqrestore(&nklock, s);

	return 0;

      error:
	xnlock_put_irqrestore(&nklock, s);

	return -err;
}

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
int sem_unlink(const char *name)
{
	cobalt_node_t *node;
	nsem_t *named_sem;
	spl_t s;
	int err;

	xnlock_get_irqsave(&nklock, s);

	err = cobalt_node_remove(&node, name, COBALT_NAMED_SEM_MAGIC);

	if (err)
		goto error;

	named_sem = node2sem(node);

	if (cobalt_node_removed_p(&named_sem->nodebase)) {
		xnlock_put_irqrestore(&nklock, s);

		sem_destroy_inner(&named_sem->sembase, cobalt_kqueues(1));
	} else
		xnlock_put_irqrestore(&nklock, s);

	return 0;

      error:
	xnlock_put_irqrestore(&nklock, s);

	return -err;
}

static inline int sem_trywait_internal(cobalt_sem_t *sem)
{
	if (sem->magic != COBALT_SEM_MAGIC)
		return -EINVAL;

#if XENO_DEBUG(POSIX)
	if (sem->owningq != sem_kqueue(sem))
		return -EPERM;
#endif /* XENO_DEBUG(POSIX) */

	if (sem->value == 0)
		return -EAGAIN;

	--sem->value;

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
static int sem_trywait(cobalt_sem_t *sem)
{
	int err;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	err = sem_trywait_internal(sem);
	xnlock_put_irqrestore(&nklock, s);

	return err;
}

static inline int
sem_timedwait_internal(cobalt_sem_t *sem, int timed, xnticks_t to)
{
	xntmode_t tmode;
	xnflags_t info;
	int ret;

	ret = sem_trywait_internal(sem);
	if (ret != -EAGAIN)
		return ret;

	if (timed) {
		tmode = sem->flags & SEM_RAWCLOCK ? XN_ABSOLUTE : XN_REALTIME;
		info = xnsynch_sleep_on(&sem->synchbase, to, tmode);
	} else
		info = xnsynch_sleep_on(&sem->synchbase, XN_INFINITE, XN_RELATIVE);

	if (info & XNRMID)
		return -EINVAL;

	if (info & XNBREAK)
		return -EINTR;

	if (info & XNTIMEO)
		return -ETIMEDOUT;

	return 0;
}

/**
 * Decrement a semaphore.
 *
 * This service decrements the semaphore @a sm if it is currently if
 * its value is greater than 0. If the semaphore's value is currently
 * zero, the calling thread is suspended until the semaphore is
 * posted, or a signal is delivered to the calling thread.
 *
 * This service is a cancellation point for Xenomai POSIX skin threads (created
 * with the pthread_create() service). When such a thread is cancelled while
 * blocked in a call to this service, the semaphore state is left unchanged
 * before the cancellation cleanup handlers are called.
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
static int sem_wait(cobalt_sem_t *sem)
{
	spl_t s;
	int err;

	xnlock_get_irqsave(&nklock, s);
	err = sem_timedwait_internal(sem, 0, XN_INFINITE);
	xnlock_put_irqrestore(&nklock, s);

	return err;
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
static int sem_timedwait(cobalt_sem_t *sem, const struct timespec *abs_timeout)
{
	spl_t s;
	int err;

	if (abs_timeout->tv_nsec > ONE_BILLION)
		return -EINVAL;

	xnlock_get_irqsave(&nklock, s);
	err = sem_timedwait_internal(sem, 1, ts2ns(abs_timeout) + 1);
	xnlock_put_irqrestore(&nklock, s);

	return err;
}

int sem_post_inner(cobalt_sem_t *sem, cobalt_kqueues_t *ownq, int bcast)
{
	if (sem->magic != COBALT_SEM_MAGIC)
		return -EINVAL;

#if XENO_DEBUG(POSIX)
	if (ownq && ownq != sem_kqueue(sem))
		return -EPERM;
#endif /* XENO_DEBUG(POSIX) */

	if (sem->value == SEM_VALUE_MAX)
		return -EINVAL;

	if (!bcast) {
		if (xnsynch_wakeup_one_sleeper(&sem->synchbase) != NULL)
			xnpod_schedule();
		else if ((sem->flags & SEM_PULSE) == 0)
			++sem->value;
	} else {
		sem->value = 0;
		if (xnsynch_flush(&sem->synchbase, 0) == XNSYNCH_RESCHED)
			xnpod_schedule();
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
static int sem_post(cobalt_sem_t *sm)
{
	int ret;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
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
int sem_getvalue(cobalt_sem_t *sem, int *value)
{
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (sem->magic != COBALT_SEM_MAGIC) {
		xnlock_put_irqrestore(&nklock, s);

		return -EINVAL;
	}

	if (sem->owningq != sem_kqueue(sem)) {
		xnlock_put_irqrestore(&nklock, s);

		return -EPERM;
	}

	if (sem->value == 0 && (sem->flags & SEM_REPORT) != 0)
		*value = -xnsynch_nsleepers(&sem->synchbase);
	else
		*value = sem->value;

	xnlock_put_irqrestore(&nklock, s);

	return 0;
}

int cobalt_sem_init(struct __shadow_sem __user *u_sem, int pshared, unsigned value)
{
	struct __shadow_sem sm;
	int err;

	if (__xn_safe_copy_from_user(&sm, u_sem, sizeof(sm)))
		return -EFAULT;

	err = do_sem_init(&sm, pshared ? SEM_PSHARED : 0, value);
	if (err < 0)
		return err;

	return __xn_safe_copy_to_user(u_sem, &sm, sizeof(*u_sem));
}

int cobalt_sem_post(struct __shadow_sem __user *u_sem)
{
	cobalt_sem_t *sm;

	__xn_get_user(sm, &u_sem->sem);

	return sem_post(sm);
}

int cobalt_sem_wait(struct __shadow_sem __user *u_sem)
{
	cobalt_sem_t *sm;

	__xn_get_user(sm, &u_sem->sem);

	return sem_wait(sm);
}

int cobalt_sem_timedwait(struct __shadow_sem __user *u_sem,
			 struct timespec __user *u_ts)
{
	struct timespec ts;
	cobalt_sem_t *sm;

	__xn_get_user(sm, &u_sem->sem);

	if (__xn_safe_copy_from_user(&ts, u_ts, sizeof(ts)))
		return -EFAULT;

	return sem_timedwait(sm, &ts);
}

int cobalt_sem_trywait(struct __shadow_sem __user *u_sem)
{
	cobalt_sem_t *sm;

	__xn_get_user(sm, &u_sem->sem);

	return sem_trywait(sm);
}

int cobalt_sem_getvalue(struct __shadow_sem __user *u_sem, int __user *u_sval)
{
	cobalt_sem_t *sm;
	int err, sval;

	__xn_get_user(sm, &u_sem->sem);

	err = sem_getvalue(sm, &sval);
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

int cobalt_sem_open(unsigned long __user *u_addr,
		    const char __user *u_name,
		    int oflags, mode_t mode, unsigned value)
{
	char name[COBALT_MAXNAME];
	struct __shadow_sem *sm;
	cobalt_assoc_t *assoc;
	unsigned long uaddr;
	cobalt_queues_t *q;
	cobalt_usem_t *usm;
	long len;
	int err;
	spl_t s;

	q = cobalt_queues();
	if (q == NULL)
		return -EPERM;

	if (__xn_safe_copy_from_user(&uaddr, u_addr, sizeof(uaddr)))
		return -EFAULT;

	len = __xn_safe_strncpy_from_user(name, u_name, sizeof(name));
	if (len < 0)
		return len;
	if (len >= sizeof(name))
		return -ENAMETOOLONG;
	if (len == 0)
		return -EINVAL;

	if (!(oflags & O_CREAT))
		sm = &((union __xeno_sem *)sem_open(name, oflags))->shadow_sem;
	else
		sm = &((union __xeno_sem *)sem_open(name, oflags, mode, value))->shadow_sem;

	if (IS_ERR(sm))
		return PTR_ERR(sm);

	xnlock_get_irqsave(&cobalt_assoc_lock, s);

	assoc = cobalt_assoc_lookup(&q->usems, (u_long)sm->sem);
	if (assoc) {
		usm = assoc2usem(assoc);
		++usm->refcnt;
		xnlock_put_irqrestore(&nklock, s);
		goto got_usm;
	}

	xnlock_put_irqrestore(&cobalt_assoc_lock, s);

	usm = xnmalloc(sizeof(*usm));
	if (usm == NULL) {
		sem_close(sm);
		return -ENOSPC;
	}

	usm->uaddr = uaddr;
	usm->refcnt = 1;

	xnlock_get_irqsave(&cobalt_assoc_lock, s);

	assoc = cobalt_assoc_lookup(&q->usems, (u_long)sm->sem);
	if (assoc) {
		assoc2usem(assoc)->refcnt++;
		xnlock_put_irqrestore(&nklock, s);
		xnfree(usm);
		usm = assoc2usem(assoc);
		goto got_usm;
	}

	cobalt_assoc_insert(&q->usems, &usm->assoc, (u_long)sm->sem);

	xnlock_put_irqrestore(&cobalt_assoc_lock, s);

      got_usm:

	if (usm->uaddr == uaddr)
		/* First binding by this process. */
		err = __xn_safe_copy_to_user((void __user *)usm->uaddr,
					     sm, sizeof(*sm));
	else
		/* Semaphore already bound by this process in user-space. */
		err = __xn_safe_copy_to_user(u_addr,
					     &usm->uaddr, sizeof(*u_addr));

	return err;
}

int cobalt_sem_close(unsigned long uaddr, int __user *u_closed)
{
	struct __shadow_sem sm;
	cobalt_assoc_t *assoc;
	int closed = 0, err;
	cobalt_usem_t *usm;
	cobalt_queues_t *q;
	spl_t s;

	q = cobalt_queues();
	if (q == NULL)
		return -EPERM;

	if (__xn_safe_copy_from_user(&sm, (void __user *)uaddr, sizeof(sm)))
		return -EFAULT;

	xnlock_get_irqsave(&cobalt_assoc_lock, s);

	assoc = cobalt_assoc_lookup(&q->usems, (u_long)sm.sem);
	if (assoc == NULL) {
		xnlock_put_irqrestore(&cobalt_assoc_lock, s);
		return -EINVAL;
	}

	usm = assoc2usem(assoc);

	err = sem_close(&sm);

	if (!err && (closed = (--usm->refcnt == 0)))
		cobalt_assoc_remove(&q->usems, (u_long)sm.sem);

	xnlock_put_irqrestore(&cobalt_assoc_lock, s);

	if (err < 0)
		return err;

	if (closed)
		xnfree(usm);

	return __xn_safe_copy_to_user(u_closed, &closed, sizeof(*u_closed));
}

int cobalt_sem_unlink(const char __user *u_name)
{
	char name[COBALT_MAXNAME];
	long len;

	len = __xn_safe_strncpy_from_user(name, u_name, sizeof(name));
	if (len < 0)
		return len;
	if (len >= sizeof(name))
		return -ENAMETOOLONG;

	return sem_unlink(name);
}

int cobalt_sem_init_np(struct __shadow_sem __user *u_sem,
		       int flags, unsigned value)
{
	struct __shadow_sem sm;
	int err;

	if (__xn_safe_copy_from_user(&sm, u_sem, sizeof(sm)))
		return -EFAULT;

	if (flags & ~(SEM_FIFO|SEM_PULSE|SEM_PSHARED|\
		      SEM_REPORT|SEM_WARNDEL|SEM_RAWCLOCK))
		return -EINVAL;

	err = do_sem_init(&sm, flags, value);
	if (err < 0)
		return err;

	return __xn_safe_copy_to_user(u_sem, &sm, sizeof(*u_sem));
}

int cobalt_sem_broadcast_np(struct __shadow_sem __user *u_sem)
{
	cobalt_sem_t *sm;
	spl_t s;
	int err;

	__xn_get_user(sm, &u_sem->sem);

	xnlock_get_irqsave(&nklock, s);
	err = sem_post_inner(sm, sm->owningq, 1);
	xnlock_put_irqrestore(&nklock, s);

	return err;
}

static void usem_cleanup(cobalt_assoc_t *assoc)
{
	struct cobalt_sem *sem = (struct cobalt_sem *) cobalt_assoc_key(assoc);
	cobalt_usem_t *usem = assoc2usem(assoc);
	nsem_t *nsem = sem2named_sem(sem);

#if XENO_DEBUG(POSIX)
	xnprintf("Posix: closing semaphore \"%s\".\n", nsem->nodebase.name);
#endif /* XENO_DEBUG(POSIX) */
	sem_close(&nsem->descriptor.shadow_sem);
	xnfree(usem);
}

void cobalt_sem_usems_cleanup(cobalt_queues_t *q)
{
	cobalt_assocq_destroy(&q->usems, &usem_cleanup);
}

void cobalt_semq_cleanup(cobalt_kqueues_t *q)
{
	xnholder_t *holder;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	while ((holder = getheadq(&q->semq)) != NULL) {
		cobalt_sem_t *sem = link2sem(holder);
		cobalt_node_t *node;
		xnlock_put_irqrestore(&nklock, s);
#if XENO_DEBUG(POSIX)
		if (sem->flags & SEM_NAMED)
			xnprintf("Posix: unlinking semaphore \"%s\".\n",
				 sem2named_sem(sem)->nodebase.name);
		else
			xnprintf("Posix: destroying semaphore %p.\n", sem);
#endif /* XENO_DEBUG(POSIX) */
		xnlock_get_irqsave(&nklock, s);
		if (sem->flags & SEM_NAMED)
			cobalt_node_remove(&node,
					  sem2named_sem(sem)->nodebase.name,
					  COBALT_NAMED_SEM_MAGIC);
		xnlock_put_irqrestore(&nklock, s);
		sem_destroy_inner(sem, q);
		xnlock_get_irqsave(&nklock, s);
	}

	xnlock_put_irqrestore(&nklock, s);
}

void cobalt_sem_pkg_init(void)
{
	initq(&cobalt_global_kqueues.semq);
}

void cobalt_sem_pkg_cleanup(void)
{
	cobalt_semq_cleanup(&cobalt_global_kqueues);
}

/*@}*/

/**
 * @file
 * This file is part of the Xenomai project.
 *
 * @note Copyright (C) 2004 Philippe Gerum <rpm@xenomai.org>
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
 *
 * \ingroup mutex
 */

/*!
 * \ingroup native
 * \defgroup mutex Mutex services.
 *
 * Mutex services.
 *
 * A mutex is a MUTual EXclusion object, and is useful for protecting
 * shared data structures from concurrent modifications, and
 * implementing critical sections and monitors.
 *
 * A mutex has two possible states: unlocked (not owned by any task),
 * and locked (owned by one task). A mutex can never be owned by two
 * different tasks simultaneously. A task attempting to lock a mutex
 * that is already locked by another task is blocked until the latter
 * unlocks the mutex first.
 *
 * Xenomai mutex services enforce a priority inheritance protocol in
 * order to solve priority inversions.
 *
 *@{*/

/** @example mutex.c */

#include <nucleus/pod.h>
#include <nucleus/registry.h>
#include <nucleus/heap.h>
#include <nucleus/sys_ppd.h>
#include <native/task.h>
#include <native/mutex.h>

#ifdef CONFIG_XENO_OPT_VFILE

struct vfile_priv {
	struct xnpholder *curr;
	char owner[XNOBJECT_NAME_LEN];
};

struct vfile_data {
	char name[XNOBJECT_NAME_LEN];
};

static int vfile_rewind(struct xnvfile_snapshot_iterator *it)
{
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	RT_MUTEX *mutex = xnvfile_priv(it->vfile);
	struct xnthread *owner;
#ifdef CONFIG_XENO_FASTSYNCH
	xnhandle_t lock_state;
#endif
	mutex = xeno_h2obj_validate(mutex, XENO_MUTEX_MAGIC, RT_MUTEX);
	if (mutex == NULL)
		return -EIDRM;

#ifdef CONFIG_XENO_FASTSYNCH
	lock_state = xnarch_atomic_get(mutex->synch_base.fastlock);
	owner = (lock_state == XN_NO_HANDLE) ? NULL :
		xnthread_lookup(xnsynch_fast_mask_claimed(lock_state));

	if (owner == NULL && lock_state != XN_NO_HANDLE)
		strncpy(priv->owner, "<DAMAGED HANDLE>",
			sizeof(priv->owner));
	else
#else /* !CONFIG_XENO_FASTSYNCH */
	owner = xnsynch_owner(&mutex->synch_base);
#endif /* !CONFIG_XENO_FASTSYNCH */
	if (owner)
		strncpy(priv->owner, xnthread_name(owner),
			sizeof(priv->owner));
	else
		*priv->owner = 0;

	priv->curr = getheadpq(xnsynch_wait_queue(&mutex->synch_base));

	return xnsynch_nsleepers(&mutex->synch_base);
}

static int vfile_next(struct xnvfile_snapshot_iterator *it, void *data)
{
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	RT_MUTEX *mutex = xnvfile_priv(it->vfile);
	struct vfile_data *p = data;
	struct xnthread *thread;

	if (priv->curr == NULL)
		return 0;	/* We are done. */

	/* Fetch current waiter, advance list cursor. */
	thread = link2thread(priv->curr, plink);
	priv->curr = nextpq(xnsynch_wait_queue(&mutex->synch_base),
			    priv->curr);
	/* Collect thread name to be output in ->show(). */
	strncpy(p->name, xnthread_name(thread), sizeof(p->name));

	return 1;
}

static int vfile_show(struct xnvfile_snapshot_iterator *it, void *data)
{
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	struct vfile_data *p = data;

	if (p == NULL) {	/* Dump header. */
		if (*priv->owner == 0)
			/* Unlocked mutex. */
			xnvfile_printf(it, "=unlocked\n");
		else {
			xnvfile_printf(it, "=locked by %.*s\n",
				       (int)sizeof(priv->owner), priv->owner);
			if (it->nrdata > 0)
				/* Mutex is contended -- dump waiters */
				xnvfile_printf(it, "--------------------\n");
		}
	} else
		xnvfile_printf(it, "%.*s\n",
			       (int)sizeof(p->name), p->name);

	return 0;
}

static struct xnvfile_snapshot_ops vfile_ops = {
	.rewind = vfile_rewind,
	.next = vfile_next,
	.show = vfile_show,
};

extern struct xnptree __native_ptree;

static struct xnpnode_snapshot __mutex_pnode = {
	.node = {
		.dirname = "mutexes",
		.root = &__native_ptree,
		.ops = &xnregistry_vfsnap_ops,
	},
	.vfile = {
		.privsz = sizeof(struct vfile_priv),
		.datasz = sizeof(struct vfile_data),
		.ops = &vfile_ops,
	},
};

#else /* !CONFIG_XENO_OPT_VFILE */

static struct xnpnode_snapshot __mutex_pnode = {
	.node = {
		.dirname = "mutexes",
	},
};

#endif /* !CONFIG_XENO_OPT_VFILE */

int rt_mutex_create_inner(RT_MUTEX *mutex, const char *name, int global)
{
	xnflags_t flags = XNSYNCH_PRIO | XNSYNCH_PIP | XNSYNCH_OWNER;
	xnarch_atomic_t *fastlock = NULL;
	int err = 0;
	spl_t s;

	if (xnpod_asynch_p())
		return -EPERM;

#ifdef CONFIG_XENO_FASTSYNCH
	/* Allocate lock memory for in-kernel use */
	fastlock = xnheap_alloc(&xnsys_ppd_get(global)->sem_heap,
				sizeof(*fastlock));

	if (!fastlock)
		return -ENOMEM;

	if (global)
		flags |= RT_MUTEX_EXPORTED;
#endif /* CONFIG_XENO_FASTSYNCH */

	xnsynch_init(&mutex->synch_base, flags, fastlock);
	mutex->handle = 0;	/* i.e. (still) unregistered mutex. */
	mutex->magic = XENO_MUTEX_MAGIC;
	mutex->lockcnt = 0;
	xnobject_copy_name(mutex->name, name);
	inith(&mutex->rlink);
	mutex->rqueue = &xeno_get_rholder()->mutexq;
	xnlock_get_irqsave(&nklock, s);
	appendq(mutex->rqueue, &mutex->rlink);
	xnlock_put_irqrestore(&nklock, s);

#ifdef CONFIG_XENO_OPT_PERVASIVE
	mutex->cpid = 0;
#endif /* CONFIG_XENO_OPT_PERVASIVE */

	/*
	 * <!> Since xnregister_enter() may reschedule, only register
	 * complete objects, so that the registry cannot return
	 * handles to half-baked objects...
	 */
	if (name) {
		err = xnregistry_enter(mutex->name, mutex, &mutex->handle,
				       &__mutex_pnode.node);

		if (err)
			rt_mutex_delete(mutex);
	}

	return err;
}

/**
 * @fn int rt_mutex_create(RT_MUTEX *mutex,const char *name)
 *
 * @brief Create a mutex.
 *
 * Create a mutual exclusion object that allows multiple tasks to
 * synchronize access to a shared resource. A mutex is left in an
 * unlocked state after creation.
 *
 * @param mutex The address of a mutex descriptor Xenomai will use to
 * store the mutex-related data.  This descriptor must always be valid
 * while the mutex is active therefore it must be allocated in
 * permanent memory.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * mutex. When non-NULL and non-empty, this string is copied to a safe
 * place into the descriptor, and passed to the registry package if
 * enabled for indexing the created mutex.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -ENOMEM is returned if the system fails to get enough dynamic
 * memory from the global real-time heap in order to register the
 * mutex.
 *
 * - -EEXIST is returned if the @a name is already in use by some
 * registered object.
 *
 * - -EPERM is returned if this service was called from an
 * asynchronous context.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: possible.
 */

int rt_mutex_create(RT_MUTEX *mutex, const char *name)
{
	return rt_mutex_create_inner(mutex, name, 1);
}

/**
 * @fn int rt_mutex_delete(RT_MUTEX *mutex)
 *
 * @brief Delete a mutex.
 *
 * Destroy a mutex and release all the tasks currently pending on it.
 * A mutex exists in the system since rt_mutex_create() has been
 * called to create it, so this service must be called in order to
 * destroy it afterwards.
 *
 * @param mutex The descriptor address of the affected mutex.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a mutex is not a mutex descriptor.
 *
 * - -EIDRM is returned if @a mutex is a deleted mutex descriptor.
 *
 * - -EPERM is returned if this service was called from an
 * asynchronous context.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: possible.
 */

int rt_mutex_delete(RT_MUTEX *mutex)
{
	int err = 0, global = 0, rc;
	spl_t s;

	if (xnpod_asynch_p())
		return -EPERM;

	xnlock_get_irqsave(&nklock, s);

	mutex = xeno_h2obj_validate(mutex, XENO_MUTEX_MAGIC, RT_MUTEX);

	if (!mutex) {
		err = xeno_handle_error(mutex, XENO_MUTEX_MAGIC, RT_MUTEX);
		goto unlock_and_exit;
	}

	global = xnsynch_test_flags(&mutex->synch_base, RT_MUTEX_EXPORTED);

	removeq(mutex->rqueue, &mutex->rlink);

	rc = xnsynch_destroy(&mutex->synch_base);

	if (mutex->handle)
		xnregistry_remove(mutex->handle);

	xeno_mark_deleted(mutex);

	if (rc == XNSYNCH_RESCHED)
		/* Some task has been woken up as a result of the deletion:
		   reschedule now. */
		xnpod_schedule();

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

#ifdef CONFIG_XENO_FASTSYNCH
	if (!err)
		xnheap_free(&xnsys_ppd_get(global)->sem_heap,
			    mutex->synch_base.fastlock);
#endif /* CONFIG_XENO_FASTSYNCH */

	return err;
}

int rt_mutex_acquire_inner(RT_MUTEX *mutex, RTIME timeout,
			   xntmode_t timeout_mode)
{
	xnthread_t *thread;
	xnflags_t info;

	if (xnpod_unblockable_p())
		return -EPERM;

	mutex = xeno_h2obj_validate(mutex, XENO_MUTEX_MAGIC, RT_MUTEX);

	if (!mutex)
		return xeno_handle_error(mutex, XENO_MUTEX_MAGIC, RT_MUTEX);

	thread = xnpod_current_thread();

	if (xnsynch_owner_check(&mutex->synch_base, thread) == 0) {
		mutex->lockcnt++;
		return 0;
	}

	if (timeout == TM_NONBLOCK && timeout_mode == XN_RELATIVE) {
#ifdef CONFIG_XENO_FASTSYNCH
		if (xnsynch_fast_acquire(mutex->synch_base.fastlock,
					 xnthread_handle(thread)) == 0) {
			if (xnthread_test_state(thread, XNOTHER))
				xnthread_inc_rescnt(thread);
			mutex->lockcnt = 1;
			return 0;
		} else
			return -EWOULDBLOCK;

#else /* !CONFIG_XENO_FASTSYNCH */
		int err = 0;
		spl_t s;

		xnlock_get_irqsave(&nklock, s);
		if (xnsynch_owner(&mutex->synch_base) == NULL) {
			if (xnthread_test_state(thread, XNOTHER))
				xnthread_inc_rescnt(thread);
			mutex->lockcnt = 1;
		} else
			err = -EWOULDBLOCK;
		xnlock_put_irqrestore(&nklock, s);
		return err;
#endif /* !CONFIG_XENO_FASTSYNCH */
	}

	info = xnsynch_acquire(&mutex->synch_base,
			       timeout, timeout_mode);
	if (unlikely(info)) {
		if (info & XNBREAK)
			return -EINTR;
		else if (info & XNTIMEO)
			return -ETIMEDOUT;
		else /* XNRMID */
			return -EIDRM;
	}

	mutex->lockcnt = 1;

	return 0;
}

/**
 * @fn int rt_mutex_acquire(RT_MUTEX *mutex, RTIME timeout)
 *
 * @brief Acquire a mutex.
 *
 * Attempt to lock a mutex. The calling task is blocked until the
 * mutex is available, in which case it is locked again before this
 * service returns. Mutexes have an ownership property, which means
 * that their current owner is tracked. Xenomai mutexes are implicitely
 * recursive and implement the priority inheritance protocol.
 *
 * Since a nested locking count is maintained for the current owner,
 * rt_mutex_acquire{_until}() and rt_mutex_release() must be used in
 * pairs.
 *
 * Tasks pend on mutexes by priority order.
 *
 * @param mutex The descriptor address of the mutex to acquire.
 *
 * @param timeout The number of clock ticks to wait for the mutex to
 * be available to the calling task (see note). Passing TM_INFINITE
 * causes the caller to block indefinitely until the mutex is
 * available. Passing TM_NONBLOCK causes the service to return
 * immediately without waiting if the mutex is still locked by another
 * task.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a mutex is not a mutex descriptor.
 *
 * - -EIDRM is returned if @a mutex is a deleted mutex descriptor,
 * including if the deletion occurred while the caller was sleeping on
 * it.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to TM_NONBLOCK
 * and the mutex is not immediately available.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * waiting task before the mutex has become available.
 *
 * - -ETIMEDOUT is returned if the mutex cannot be made available to
 * the calling task within the specified amount of time.
 *
 * - -EPERM is returned if this service was called from a context
 * which cannot be given the ownership of the mutex (e.g. interrupt,
 * non-realtime context).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel-based task
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always unless the request is immediately satisfied or
 * @a timeout specifies a non-blocking operation.  If the caller is
 * blocked, the current owner's priority might be temporarily raised
 * as a consequence of the priority inheritance protocol.
 *
 * @note The @a timeout value will be interpreted as jiffies if the
 * native skin is bound to a periodic time base (see
 * CONFIG_XENO_OPT_NATIVE_PERIOD), or nanoseconds otherwise.
 */

int rt_mutex_acquire(RT_MUTEX *mutex, RTIME timeout)
{
	return rt_mutex_acquire_inner(mutex, timeout, XN_RELATIVE);
}

/**
 * @fn int rt_mutex_acquire_until(RT_MUTEX *mutex, RTIME timeout)
 *
 * @brief Acquire a mutex (with absolute timeout date).
 *
 * Attempt to lock a mutex. The calling task is blocked until the
 * mutex is available, in which case it is locked again before this
 * service returns. Mutexes have an ownership property, which means
 * that their current owner is tracked. Xenomai mutexes are implicitely
 * recursive and implement the priority inheritance protocol.
 *
 * Since a nested locking count is maintained for the current owner,
 * rt_mutex_acquire{_until}() and rt_mutex_release() must be used in
 * pairs.
 *
 * Tasks pend on mutexes by priority order.
 *
 * @param mutex The descriptor address of the mutex to acquire.
 *
 * @param timeout The absolute date specifying a time limit to wait
 * for the mutex to be available to the calling task (see
 * note).
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a mutex is not a mutex descriptor.
 *
 * - -EIDRM is returned if @a mutex is a deleted mutex descriptor,
 * including if the deletion occurred while the caller was sleeping on
 * it.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to TM_NONBLOCK
 * and the mutex is not immediately available.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * waiting task before the mutex has become available.
 *
 * - -ETIMEDOUT is returned if the mutex cannot be made available to
 * the calling task until the absolute timeout date is reached.
 *
 * - -EPERM is returned if this service was called from a context
 * which cannot be given the ownership of the mutex (e.g. interrupt,
 * non-realtime context).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel-based task
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always unless the request is immediately satisfied or
 * @a timeout specifies a non-blocking operation.  If the caller is
 * blocked, the current owner's priority might be temporarily raised
 * as a consequence of the priority inheritance protocol.
 *
 * @note The @a timeout value will be interpreted as jiffies if the
 * native skin is bound to a periodic time base (see
 * CONFIG_XENO_OPT_NATIVE_PERIOD), or nanoseconds otherwise.
 */

int rt_mutex_acquire_until(RT_MUTEX *mutex, RTIME timeout)
{
	return rt_mutex_acquire_inner(mutex, timeout, XN_REALTIME);
}

/**
 * @fn int rt_mutex_release(RT_MUTEX *mutex)
 *
 * @brief Unlock mutex.
 *
 * Release a mutex. If the mutex is pended, the first waiting task (by
 * priority order) is immediately unblocked and transfered the
 * ownership of the mutex; otherwise, the mutex is left in an unlocked
 * state.
 *
 * @param mutex The descriptor address of the released mutex.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a mutex is not a mutex descriptor.
 *
 * - -EIDRM is returned if @a mutex is a deleted mutex descriptor.
 *
 * - -EPERM is returned if @a mutex is not owned by the current task,
 * or more generally if this service was called from a context which
 * cannot own any mutex (e.g. interrupt, or non-realtime context).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel-based task
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: possible.
 */

int rt_mutex_release(RT_MUTEX *mutex)
{
	xnthread_t *thread = xnpod_current_thread();
	int err;

	if (xnpod_unblockable_p())
		return -EPERM;

	mutex = xeno_h2obj_validate(mutex, XENO_MUTEX_MAGIC, RT_MUTEX);

	if (!mutex)
		return xeno_handle_error(mutex, XENO_MUTEX_MAGIC, RT_MUTEX);

	err = xnsynch_owner_check(&mutex->synch_base, thread);
	if (err)
		return err;

	if (--mutex->lockcnt > 0)
		return 0;

	if (xnsynch_release(&mutex->synch_base))
		xnpod_schedule();

	return 0;
}

/**
 * @fn int rt_mutex_inquire(RT_MUTEX *mutex, RT_MUTEX_INFO *info)
 *
 * @brief Inquire about a mutex.
 *
 * Return various information about the status of a given mutex.
 *
 * @param mutex The descriptor address of the inquired mutex.
 *
 * @param info The address of a structure the mutex information will
 * be written to.

 * @return 0 is returned and status information is written to the
 * structure pointed at by @a info upon success. Otherwise:
 *
 * - -EINVAL is returned if @a mutex is not a mutex descriptor.
 *
 * - -EIDRM is returned if @a mutex is a deleted mutex descriptor.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

int rt_mutex_inquire(RT_MUTEX *mutex, RT_MUTEX_INFO *info)
{
#ifdef CONFIG_XENO_FASTSYNCH
	xnhandle_t lock_state;
#endif /* CONFIG_XENO_FASTSYNCH */
	xnthread_t *owner;
	int err = 0;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	mutex = xeno_h2obj_validate(mutex, XENO_MUTEX_MAGIC, RT_MUTEX);

	if (!mutex) {
		err = xeno_handle_error(mutex, XENO_MUTEX_MAGIC, RT_MUTEX);
		goto unlock_and_exit;
	}

	strcpy(info->name, mutex->name);
	info->nwaiters = xnsynch_nsleepers(&mutex->synch_base);

#ifndef CONFIG_XENO_FASTSYNCH
	owner = xnsynch_owner(&mutex->synch_base);
#else /* CONFIG_XENO_FASTSYNCH */
	lock_state = xnarch_atomic_get(mutex->synch_base.fastlock);
	info->locked = (lock_state != XN_NO_HANDLE);
	owner = (info->locked) ?
		xnthread_lookup(xnsynch_fast_mask_claimed(lock_state)) : NULL;
	if (!owner && info->locked)
		strcpy(info->owner, "<DAMAGED HANDLE!>");
	else
#endif /* CONFIG_XENO_FASTSYNCH */
	if (owner)
		strcpy(info->owner, xnthread_name(owner));
	else
		info->owner[0] = 0;

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

/**
 * @fn int rt_mutex_bind(RT_MUTEX *mutex,const char *name,RTIME timeout)
 *
 * @brief Bind to a mutex.
 *
 * This user-space only service retrieves the uniform descriptor of a
 * given Xenomai mutex identified by its symbolic name. If the mutex does
 * not exist on entry, this service blocks the caller until a mutex of
 * the given name is created.
 *
 * @param name A valid NULL-terminated name which identifies the
 * mutex to bind to.
 *
 * @param mutex The address of a mutex descriptor retrieved by the
 * operation. Contents of this memory is undefined upon failure.
 *
 * @param timeout The number of clock ticks to wait for the
 * registration to occur (see note). Passing TM_INFINITE causes the
 * caller to block indefinitely until the object is
 * registered. Passing TM_NONBLOCK causes the service to return
 * immediately without waiting if the object is not registered on
 * entry.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EFAULT is returned if @a mutex or @a name is referencing invalid
 * memory.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * waiting task before the retrieval has completed.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to TM_NONBLOCK
 * and the searched object is not registered on entry.
 *
 * - -ETIMEDOUT is returned if the object cannot be retrieved within
 * the specified amount of time.
 *
 * - -EPERM is returned if this service should block, but was called
 * from a context which cannot sleep (e.g. interrupt, non-realtime
 * context).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always unless the request is immediately satisfied or
 * @a timeout specifies a non-blocking operation.
 *
 * @note The @a timeout value will be interpreted as jiffies if the
 * native skin is bound to a periodic time base (see
 * CONFIG_XENO_OPT_NATIVE_PERIOD), or nanoseconds otherwise.
 */

/**
 * @fn int rt_mutex_unbind(RT_MUTEX *mutex)
 *
 * @brief Unbind from a mutex.
 *
 * This user-space only service unbinds the calling task from the
 * mutex object previously retrieved by a call to rt_mutex_bind().
 *
 * @param mutex The address of a mutex descriptor to unbind from.
 *
 * @return 0 is always returned.
 *
 * This service can be called from:
 *
 * - User-space task.
 *
 * Rescheduling: never.
 */

int __native_mutex_pkg_init(void)
{
	return 0;
}

void __native_mutex_pkg_cleanup(void)
{
	__native_mutex_flush_rq(&__native_global_rholder.mutexq);
}

/*@}*/

EXPORT_SYMBOL_GPL(rt_mutex_create);
EXPORT_SYMBOL_GPL(rt_mutex_delete);
EXPORT_SYMBOL_GPL(rt_mutex_acquire);
EXPORT_SYMBOL_GPL(rt_mutex_acquire_until);
EXPORT_SYMBOL_GPL(rt_mutex_release);
EXPORT_SYMBOL_GPL(rt_mutex_inquire);

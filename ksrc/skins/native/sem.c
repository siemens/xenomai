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
 * \ingroup semaphore
 */

/*!
 * \ingroup native
 * \defgroup semaphore Counting semaphore services.
 *
 * A counting semaphore is a synchronization object granting Xenomai
 * tasks a concurrent access to a given number of resources maintained
 * in an internal counter variable. The semaphore is used through the
 * P ("Proberen", from the Dutch "test and decrement") and V
 * ("Verhogen", increment) operations. The P operation waits for a
 * unit to become available from the count, and the V operation
 * releases a resource by incrementing the unit count by one.
 *
 * If no more than a single resource is made available at any point in
 * time, the semaphore enforces mutual exclusion and thus can be used
 * to serialize access to a critical section. However, mutexes should
 * be used instead in order to prevent priority inversions.
 *
 *@{*/

/** @example semaphore.c */

#include <nucleus/pod.h>
#include <nucleus/registry.h>
#include <nucleus/heap.h>
#include <native/task.h>
#include <native/sem.h>

#ifdef CONFIG_XENO_OPT_VFILE

struct vfile_priv {
	struct xnpholder *curr;
	unsigned long count;
};

struct vfile_data {
	char name[XNOBJECT_NAME_LEN];
};

static int vfile_rewind(struct xnvfile_snapshot_iterator *it)
{
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	RT_SEM *sem = xnvfile_priv(it->vfile);

	sem = xeno_h2obj_validate(sem, XENO_SEM_MAGIC, RT_SEM);
	if (sem == NULL)
		return -EIDRM;

	priv->curr = getheadpq(xnsynch_wait_queue(&sem->synch_base));
	priv->count = sem->count;

	return xnsynch_nsleepers(&sem->synch_base);
}

static int vfile_next(struct xnvfile_snapshot_iterator *it, void *data)
{
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	RT_SEM *sem = xnvfile_priv(it->vfile);
	struct vfile_data *p = data;
	struct xnthread *thread;

	/*
	 * Refresh the semaphore count as we get waiters, to reduce
	 * the odds for inconsistency (that value may change while
	 * collecting records, and we don't want to touch the revision
	 * tag each time that value changes).
	 */
	priv->count = sem->count;

	if (priv->curr == NULL)
		return 0;	/* We are done. */

	/* Fetch current waiter, advance list cursor. */
	thread = link2thread(priv->curr, plink);
	priv->curr = nextpq(xnsynch_wait_queue(&sem->synch_base),
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
		if (it->nrdata == 0)
			/* Idle/posted semaphore -- just dump count. */
			xnvfile_printf(it, "=%lu\n", priv->count);
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

static struct xnpnode_snapshot __sem_pnode = {
	.node = {
		.dirname = "semaphores",
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

static struct xnpnode_snapshot __sem_pnode = {
	.node = {
		.dirname = "semaphores",
	},
};

#endif /* !CONFIG_XENO_OPT_VFILE */

/**
 * @fn int rt_sem_create(RT_SEM *sem,const char *name,unsigned long icount,int mode)
 * @brief Create a counting semaphore.
 *
 * @param sem The address of a semaphore descriptor Xenomai will use to
 * store the semaphore-related data.  This descriptor must always be
 * valid while the semaphore is active therefore it must be allocated
 * in permanent memory.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * semaphore. When non-NULL and non-empty, this string is copied to a
 * safe place into the descriptor, and passed to the registry package
 * if enabled for indexing the created semaphore.
 *
 * @param icount The initial value of the semaphore count.
 *
 * @param mode The semaphore creation mode. The following flags can be
 * OR'ed into this bitmask, each of them affecting the new semaphore:
 *
 * - S_FIFO makes tasks pend in FIFO order on the semaphore.
 *
 * - S_PRIO makes tasks pend in priority order on the semaphore.
 *
 * - S_PULSE causes the semaphore to behave in "pulse" mode. In this
 * mode, the V (signal) operation attempts to release a single waiter
 * each time it is called, but without incrementing the semaphore
 * count if no waiter is pending. For this reason, the semaphore count
 * in pulse mode remains zero.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -ENOMEM is returned if the system fails to get enough dynamic
 * memory from the global real-time heap in order to register the
 * semaphore.
 *
 * - -EEXIST is returned if the @a name is already in use by some
 * registered object.
 *
 * - -EINVAL is returned if the @a icount is non-zero and @a mode
 * specifies a pulse semaphore.
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

int rt_sem_create(RT_SEM *sem, const char *name, unsigned long icount, int mode)
{
	int err = 0;
	spl_t s;

	if (xnpod_asynch_p())
		return -EPERM;

	if ((mode & S_PULSE) && icount > 0)
		return -EINVAL;

	xnsynch_init(&sem->synch_base, mode & S_PRIO, NULL);
	sem->count = icount;
	sem->mode = mode;
	sem->handle = 0;	/* i.e. (still) unregistered semaphore. */
	sem->magic = XENO_SEM_MAGIC;
	xnobject_copy_name(sem->name, name);
	inith(&sem->rlink);
	sem->rqueue = &xeno_get_rholder()->semq;
	xnlock_get_irqsave(&nklock, s);
	appendq(sem->rqueue, &sem->rlink);
	xnlock_put_irqrestore(&nklock, s);

#ifdef CONFIG_XENO_OPT_PERVASIVE
	sem->cpid = 0;
#endif /* CONFIG_XENO_OPT_PERVASIVE */

	/*
	 * <!> Since xnregister_enter() may reschedule, only register
	 * complete objects, so that the registry cannot return
	 * handles to half-baked objects...
	 */
	if (name) {
		err = xnregistry_enter(sem->name, sem, &sem->handle,
				       &__sem_pnode.node);
		if (err)
			rt_sem_delete(sem);
	}

	return err;
}

/**
 * @fn int rt_sem_delete(RT_SEM *sem)
 * @brief Delete a semaphore.
 *
 * Destroy a semaphore and release all the tasks currently pending on
 * it.  A semaphore exists in the system since rt_sem_create() has
 * been called to create it, so this service must be called in order
 * to destroy it afterwards.
 *
 * @param sem The descriptor address of the affected semaphore.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a sem is not a semaphore descriptor.
 *
 * - -EIDRM is returned if @a sem is a deleted semaphore descriptor.
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

int rt_sem_delete(RT_SEM *sem)
{
	int err = 0, rc;
	spl_t s;

	if (xnpod_asynch_p())
		return -EPERM;

	xnlock_get_irqsave(&nklock, s);

	sem = xeno_h2obj_validate(sem, XENO_SEM_MAGIC, RT_SEM);

	if (!sem) {
		err = xeno_handle_error(sem, XENO_SEM_MAGIC, RT_SEM);
		goto unlock_and_exit;
	}

	removeq(sem->rqueue, &sem->rlink);

	rc = xnsynch_destroy(&sem->synch_base);

	if (sem->handle)
		xnregistry_remove(sem->handle);

	xeno_mark_deleted(sem);

	if (rc == XNSYNCH_RESCHED)
		/* Some task has been woken up as a result of the deletion:
		   reschedule now. */
		xnpod_schedule();

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

int rt_sem_p_inner(RT_SEM *sem, xntmode_t timeout_mode, RTIME timeout)
{
	xnflags_t info;
	int err = 0;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	sem = xeno_h2obj_validate(sem, XENO_SEM_MAGIC, RT_SEM);

	if (!sem) {
		err = xeno_handle_error(sem, XENO_SEM_MAGIC, RT_SEM);
		goto unlock_and_exit;
	}

	if (timeout == TM_NONBLOCK) {
		if (sem->count > 0)
			sem->count--;
		else
			err = -EWOULDBLOCK;

		goto unlock_and_exit;
	}

	if (xnpod_unblockable_p()) {
		err = -EPERM;
		goto unlock_and_exit;
	}

	if (sem->count > 0)
		--sem->count;
	else {
		info = xnsynch_sleep_on(&sem->synch_base,
					timeout, timeout_mode);
		if (info & XNRMID)
			err = -EIDRM;	/* Semaphore deleted while pending. */
		else if (info & XNTIMEO)
			err = -ETIMEDOUT;	/* Timeout. */
		else if (info & XNBREAK)
			err = -EINTR;	/* Unblocked. */
	}

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

/**
 * @fn int rt_sem_p(RT_SEM *sem, RTIME timeout)
 * @brief Pend on a semaphore.
 *
 * Acquire a semaphore unit. If the semaphore value is greater than
 * zero, it is decremented by one and the service immediately returns
 * to the caller. Otherwise, the caller is blocked until the semaphore
 * is either signaled or destroyed, unless a non-blocking operation
 * has been required.
 *
 * @param sem The descriptor address of the affected semaphore.
 *
 * @param timeout The number of clock ticks to wait for a semaphore
 * unit to be available (see note). Passing TM_INFINITE causes the
 * caller to block indefinitely until a unit is available. Passing
 * TM_NONBLOCK causes the service to return immediately without
 * waiting if no unit is available.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a sem is not a semaphore descriptor.
 *
 * - -EIDRM is returned if @a sem is a deleted semaphore descriptor,
 * including if the deletion occurred while the caller was sleeping on
 * it for a unit to become available.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to TM_NONBLOCK
 * and the semaphore value is zero.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * waiting task before a semaphore unit has become available.
 *
 * - -ETIMEDOUT is returned if no unit is available within the
 * specified amount of time.
 *
 * - -EPERM is returned if this service should block, but was called
 * from a context which cannot sleep (e.g. interrupt, non-realtime
 * context).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 *   only if @a timeout is equal to TM_NONBLOCK.
 *
 * - Kernel-based task
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always unless the request is immediately satisfied or
 * @a timeout specifies a non-blocking operation.
 *
 * @note The @a timeout value will be interpreted as jiffies if the
 * native skin is bound to a periodic time base (see
 * CONFIG_XENO_OPT_NATIVE_PERIOD), or nanoseconds otherwise.
 */

int rt_sem_p(RT_SEM *sem, RTIME timeout)
{
	return rt_sem_p_inner(sem, XN_RELATIVE, timeout);
}

/**
 * @fn int rt_sem_p_until(RT_SEM *sem, RTIME timeout)
 * @brief Pend on a semaphore (with absolute timeout date).
 *
 * Acquire a semaphore unit. If the semaphore value is greater than
 * zero, it is decremented by one and the service immediately returns
 * to the caller. Otherwise, the caller is blocked until the semaphore
 * is either signaled or destroyed, unless a non-blocking operation
 * has been required.
 *
 * @param sem The descriptor address of the affected semaphore.
 *
 * @param timeout The absolute date specifying a time limit to wait
 * for a semaphore unit to be available (see note). Passing
 * TM_INFINITE causes the caller to block indefinitely until a unit is
 * available. Passing TM_NONBLOCK causes the service to return
 * immediately without waiting if no unit is available.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a sem is not a semaphore descriptor.
 *
 * - -EIDRM is returned if @a sem is a deleted semaphore descriptor,
 * including if the deletion occurred while the caller was sleeping on
 * it for a unit to become available.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to TM_NONBLOCK
 * and the semaphore value is zero.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * waiting task before a semaphore unit has become available.
 *
 * - -ETIMEDOUT is returned if the absolute @a timeout date is reached
 * before a semaphore unit is available.

 * - -EPERM is returned if this service should block, but was called
 * from a context which cannot sleep (e.g. interrupt, non-realtime
 * context).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 *   only if @a timeout is equal to TM_NONBLOCK.
 *
 * - Kernel-based task
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always unless the request is immediately satisfied or
 * @a timeout specifies a non-blocking operation.
 *
 * @note The @a timeout value will be interpreted as jiffies if the
 * native skin is bound to a periodic time base (see
 * CONFIG_XENO_OPT_NATIVE_PERIOD), or nanoseconds otherwise.
 */

int rt_sem_p_until(RT_SEM *sem, RTIME timeout)
{
	return rt_sem_p_inner(sem, XN_REALTIME, timeout);
}

/**
 * @fn int rt_sem_v(RT_SEM *sem)
 * @brief Signal a semaphore.
 *
 * Release a semaphore unit. If the semaphore is pended, the first
 * waiting task (by queuing order) is immediately unblocked;
 * otherwise, the semaphore value is incremented by one.
 *
 * @param sem The descriptor address of the affected semaphore.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a sem is not a semaphore descriptor.
 *
 * - -EIDRM is returned if @a sem is a deleted semaphore descriptor.
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
 * Rescheduling: possible.
 */

int rt_sem_v(RT_SEM *sem)
{
	int err = 0;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	sem = xeno_h2obj_validate(sem, XENO_SEM_MAGIC, RT_SEM);

	if (!sem) {
		err = xeno_handle_error(sem, XENO_SEM_MAGIC, RT_SEM);
		goto unlock_and_exit;
	}

	if (xnsynch_wakeup_one_sleeper(&sem->synch_base) != NULL)
		xnpod_schedule();
	else if (!(sem->mode & S_PULSE))
		sem->count++;

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

/**
 * @fn int rt_sem_broadcast(RT_SEM *sem)
 * @brief Broadcast a semaphore.
 *
 * Unblock all tasks waiting on a semaphore. Awaken tasks return from
 * rt_sem_p() as if the semaphore has been signaled. The semaphore
 * count is zeroed as a result of the operation.
 *
 * @param sem The descriptor address of the affected semaphore.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a sem is not a semaphore descriptor.
 *
 * - -EIDRM is returned if @a sem is a deleted semaphore descriptor.
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
 * Rescheduling: possible.
 */

int rt_sem_broadcast(RT_SEM *sem)
{
	int err = 0;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	sem = xeno_h2obj_validate(sem, XENO_SEM_MAGIC, RT_SEM);

	if (!sem) {
		err = xeno_handle_error(sem, XENO_SEM_MAGIC, RT_SEM);
		goto unlock_and_exit;
	}

	if (xnsynch_flush(&sem->synch_base, 0) == XNSYNCH_RESCHED)
		xnpod_schedule();

	sem->count = 0;

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

/**
 * @fn int rt_sem_inquire(RT_SEM *sem, RT_SEM_INFO *info)
 * @brief Inquire about a semaphore.
 *
 * Return various information about the status of a given semaphore.
 *
 * @param sem The descriptor address of the inquired semaphore.
 *
 * @param info The address of a structure the semaphore information
 * will be written to.

 * @return 0 is returned and status information is written to the
 * structure pointed at by @a info upon success. Otherwise:
 *
 * - -EINVAL is returned if @a sem is not a semaphore descriptor.
 *
 * - -EIDRM is returned if @a sem is a deleted semaphore descriptor.
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

int rt_sem_inquire(RT_SEM *sem, RT_SEM_INFO *info)
{
	int err = 0;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	sem = xeno_h2obj_validate(sem, XENO_SEM_MAGIC, RT_SEM);

	if (!sem) {
		err = xeno_handle_error(sem, XENO_SEM_MAGIC, RT_SEM);
		goto unlock_and_exit;
	}

	strcpy(info->name, sem->name);
	info->count = sem->count;
	info->nwaiters = xnsynch_nsleepers(&sem->synch_base);

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

/**
 * @fn int rt_sem_bind(RT_SEM *sem,const char *name,RTIME timeout)
 * @brief Bind to a semaphore.
 *
 * This user-space only service retrieves the uniform descriptor of a
 * given Xenomai semaphore identified by its symbolic name. If the
 * semaphore does not exist on entry, this service blocks the caller
 * until a semaphore of the given name is created.
 *
 * @param name A valid NULL-terminated name which identifies the
 * semaphore to bind to.
 *
 * @param sem The address of a semaphore descriptor retrieved by the
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
 * - -EFAULT is returned if @a sem or @a name is referencing invalid
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
 * @fn int rt_sem_unbind(RT_SEM *sem)
 *
 * @brief Unbind from a semaphore.
 *
 * This user-space only service unbinds the calling task from the
 * semaphore object previously retrieved by a call to rt_sem_bind().
 *
 * @param sem The address of a semaphore descriptor to unbind from.
 *
 * @return 0 is always returned.
 *
 * This service can be called from:
 *
 * - User-space task.
 *
 * Rescheduling: never.
 */

int __native_sem_pkg_init(void)
{
	return 0;
}

void __native_sem_pkg_cleanup(void)
{
	__native_sem_flush_rq(&__native_global_rholder.semq);
}

/*@}*/

EXPORT_SYMBOL_GPL(rt_sem_create);
EXPORT_SYMBOL_GPL(rt_sem_delete);
EXPORT_SYMBOL_GPL(rt_sem_p);
EXPORT_SYMBOL_GPL(rt_sem_p_until);
EXPORT_SYMBOL_GPL(rt_sem_v);
EXPORT_SYMBOL_GPL(rt_sem_inquire);
EXPORT_SYMBOL_GPL(rt_sem_broadcast);

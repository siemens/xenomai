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
 * @defgroup cobalt_mutex Mutex services.
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
 * By default, Cobalt mutexes are of the normal type, use no
 * priority protocol and may not be shared between several processes.
 *
 * Note that only pthread_mutex_init() may be used to initialize a mutex, using
 * the static initializer @a PTHREAD_MUTEX_INITIALIZER is not supported.
 *
 *@{*/

#include "internal.h"
#include "thread.h"
#include "mutex.h"
#include "cond.h"
#include "clock.h"

static int cobalt_mutex_init_inner(struct cobalt_mutex_shadow *shadow,
				   struct cobalt_mutex *mutex,
				   struct mutex_dat *datp,
				   const pthread_mutexattr_t *attr)
{
	int synch_flags = XNSYNCH_PRIO | XNSYNCH_OWNER;
	struct xnsys_ppd *sys_ppd;
	struct cobalt_kqueues *kq;
	spl_t s;
	int err;

	if (!attr)
		attr = &cobalt_default_mutex_attr;

	if (attr->magic != COBALT_MUTEX_ATTR_MAGIC)
		return -EINVAL;

	kq = cobalt_kqueues(attr->pshared);
	sys_ppd = xnsys_ppd_get(attr->pshared);
	err = xnregistry_enter_anon(mutex, &shadow->handle);
	if (err < 0)
		return err;

	mutex->handle = shadow->handle;
	shadow->magic = COBALT_MUTEX_MAGIC;
	shadow->lockcnt = 0;

	shadow->attr = *attr;
	shadow->dat_offset = xnheap_mapped_offset(&sys_ppd->sem_heap, datp);

	if (attr->protocol == PTHREAD_PRIO_INHERIT)
		synch_flags |= XNSYNCH_PIP;

	mutex->magic = COBALT_MUTEX_MAGIC;
	xnsynch_init(&mutex->synchbase, synch_flags, &datp->owner);
	datp->flags = (attr->type == PTHREAD_MUTEX_ERRORCHECK
		       ? COBALT_MUTEX_ERRORCHECK : 0);
	mutex->attr = *attr;
	mutex->owningq = kq;
	INIT_LIST_HEAD(&mutex->conds);

	xnlock_get_irqsave(&nklock, s);
	list_add_tail(&mutex->link, &kq->mutexq);
	xnlock_put_irqrestore(&nklock, s);

	return 0;
}

static void 
cobalt_mutex_destroy_inner(xnhandle_t handle, struct cobalt_kqueues *q)
{
	struct cobalt_mutex *mutex;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	mutex = xnregistry_lookup(handle, NULL);
	if (!cobalt_obj_active(mutex, COBALT_MUTEX_MAGIC, typeof(*mutex))) {
		xnlock_put_irqrestore(&nklock, s);
		printk("mutex_destroy: invalid mutex %x\n", 
			mutex ? mutex->magic : ~0);
		return;
	}
	xnregistry_remove(handle);
	list_del(&mutex->link);
	/*
	 * synchbase wait queue may not be empty only when this
	 * function is called from cobalt_mutex_pkg_cleanup, hence the
	 * absence of xnsched_run().
	 */
	xnsynch_destroy(&mutex->synchbase);
	cobalt_mark_deleted(mutex);
	xnlock_put_irqrestore(&nklock, s);

	xnheap_free(&xnsys_ppd_get(mutex->attr.pshared)->sem_heap,
		    mutex->synchbase.fastlock);
	xnfree(mutex);
}

static inline int cobalt_mutex_acquire(struct xnthread *cur,
				       struct cobalt_mutex *mutex,
				       int timed,
				       const struct timespec __user *u_ts)
{
	if (!cobalt_obj_active(mutex, COBALT_MUTEX_MAGIC, struct cobalt_mutex))
		return -EINVAL;

#if XENO_DEBUG(COBALT)
	if (mutex->owningq != cobalt_kqueues(mutex->attr.pshared))
		return -EPERM;
#endif /* XENO_DEBUG(COBALT) */

	if (xnsynch_owner_check(&mutex->synchbase, cur) == 0)
		return -EBUSY;

	return cobalt_mutex_acquire_unchecked(cur, mutex, timed, u_ts);
}

/* must be called with nklock locked, interrupts off. */
int cobalt_mutex_acquire_unchecked(struct xnthread *cur,
				   struct cobalt_mutex *mutex,
				   int timed,
				   const struct timespec __user *u_ts)
{
	struct timespec ts;

	if (timed) {	/* Always called with IRQs on in this case. */
		if (u_ts == NULL ||
		    __xn_safe_copy_from_user(&ts, u_ts, sizeof(ts)))
			return -EFAULT;
		if (ts.tv_nsec >= ONE_BILLION)
			return -EINVAL;
		xnsynch_acquire(&mutex->synchbase, ts2ns(&ts) + 1, XN_REALTIME);
	} else
		xnsynch_acquire(&mutex->synchbase, XN_INFINITE, XN_RELATIVE);

	if (xnthread_test_info(cur, XNBREAK | XNRMID | XNTIMEO)) {
		if (xnthread_test_info(cur, XNBREAK))
			return -EINTR;
		else if (xnthread_test_info(cur, XNTIMEO))
			return -ETIMEDOUT;
		else /* XNRMID */
			return -EINVAL;
	}

	return 0;
}

int cobalt_mutex_release(struct xnthread *cur,
			 struct cobalt_mutex *mutex)
{
	struct cobalt_cond *cond;
	struct mutex_dat *datp;
	unsigned long flags;
	int need_resched;

	if (!cobalt_obj_active(mutex, COBALT_MUTEX_MAGIC, struct cobalt_mutex))
		 return -EINVAL;

#if XENO_DEBUG(COBALT)
	if (mutex->owningq != cobalt_kqueues(mutex->attr.pshared))
		return -EPERM;
#endif /* XENO_DEBUG(COBALT) */

	datp = container_of(mutex->synchbase.fastlock, struct mutex_dat, owner);
	flags = datp->flags;
	need_resched = 0;
	if ((flags & COBALT_MUTEX_COND_SIGNAL)) {
		datp->flags = flags & ~COBALT_MUTEX_COND_SIGNAL;
		if (!list_empty(&mutex->conds)) {
			list_for_each_entry(cond, &mutex->conds, mutex_link)
				need_resched |=
				cobalt_cond_deferred_signals(cond);
		}
	}
	need_resched |= xnsynch_release(&mutex->synchbase, cur) != NULL;

	return need_resched;
}

static inline
int cobalt_mutex_timedlock_break(struct cobalt_mutex *mutex,
				 int timed, const struct timespec __user *u_ts)
{
	struct xnthread *curr = xnshadow_current();
	int ret;

	/* We need a valid thread handle for the fast lock. */
	if (xnthread_handle(curr) == XN_NO_HANDLE)
		return -EPERM;

	ret = cobalt_mutex_acquire(curr, mutex, timed, u_ts);
	if (ret != -EBUSY)
		return ret;

	switch(mutex->attr.type) {
	case PTHREAD_MUTEX_NORMAL:
		/* Attempting to relock a normal mutex, deadlock. */
#if XENO_DEBUG(COBALT)
		printk(XENO_WARN
		       "thread %s deadlocks on non-recursive mutex\n",
		       curr->name);
#endif /* XENO_DEBUG(COBALT) */
		cobalt_mutex_acquire_unchecked(curr, mutex, timed, u_ts);
		break;

		/* Recursive mutexes are handled in user-space, so
		   these cases can not happen */
	case PTHREAD_MUTEX_ERRORCHECK:
		ret = -EINVAL;
		break;

	case PTHREAD_MUTEX_RECURSIVE:
		ret = -EINVAL;
		break;
	}

	return ret;
}

int cobalt_mutex_check_init(struct cobalt_mutex_shadow __user *u_mx)
{
	struct cobalt_mutex *mutex;
	xnhandle_t handle;
	int err;
	spl_t s;

	handle = cobalt_get_handle_from_user(&u_mx->handle);

	xnlock_get_irqsave(&nklock, s);
	mutex = xnregistry_lookup(handle, NULL);
	if (cobalt_obj_active(mutex, COBALT_MUTEX_MAGIC, typeof(*mutex)))
		/* mutex is already in a queue. */
		err = -EBUSY;
	else
		err = 0;

	xnlock_put_irqrestore(&nklock, s);
	return err;
}

int cobalt_mutex_init(struct cobalt_mutex_shadow __user *u_mx,
		      const pthread_mutexattr_t __user *u_attr)
{
	const pthread_mutexattr_t *attr;
	pthread_mutexattr_t locattr;
	struct cobalt_mutex *mutex;
	struct cobalt_mutex_shadow mx;
	struct mutex_dat *datp;
	int err;

	if (__xn_safe_copy_from_user(&mx, u_mx, sizeof(mx)))
		return -EFAULT;

	if (u_attr) {
		if (__xn_safe_copy_from_user(&locattr, u_attr,
					     sizeof(locattr)))
			return -EFAULT;

		attr = &locattr;
	} else
		attr = &cobalt_default_mutex_attr;

	mutex = xnmalloc(sizeof(*mutex));
	if (mutex == NULL)
		return -ENOMEM;

	datp = xnheap_alloc(&xnsys_ppd_get(attr->pshared)->sem_heap,
			     sizeof(*datp));
	if (datp == NULL) {
		xnfree(mutex);
		return -EAGAIN;
	}

	err = cobalt_mutex_init_inner(&mx, mutex, datp, attr);
	if (err) {
		xnfree(mutex);
		xnheap_free(&xnsys_ppd_get(attr->pshared)->sem_heap, datp);
		return err;
	}

	return __xn_safe_copy_to_user(u_mx, &mx, sizeof(*u_mx));
}

int cobalt_mutex_destroy(struct cobalt_mutex_shadow __user *u_mx)
{
	struct cobalt_mutex *mutex;
	struct cobalt_mutex_shadow mx;
	spl_t s;
	int err;

	if (__xn_safe_copy_from_user(&mx, u_mx, sizeof(mx)))
		return -EFAULT;

	xnlock_get_irqsave(&nklock, s);
	mutex = xnregistry_lookup(mx.handle, NULL);
	if (!cobalt_obj_active(mutex, COBALT_MUTEX_MAGIC, typeof(*mutex))) {
		err = -EINVAL;
		goto err_unlock;
	}
	if (cobalt_kqueues(mutex->attr.pshared) != mutex->owningq) {
		err = -EPERM;
		goto err_unlock;
	}

	if (xnsynch_fast_owner_check(mutex->synchbase.fastlock,
					XN_NO_HANDLE) != 0) {
		err = -EBUSY;
		goto err_unlock;
	}

	if (!list_empty(&mutex->conds)) {
		err = -EBUSY;
	  err_unlock:
		xnlock_put_irqrestore(&nklock, s);
		return err;
	}

	cobalt_mark_deleted(&mx);
	xnlock_put_irqrestore(&nklock, s);
	cobalt_mutex_destroy_inner(mx.handle, mutex->owningq);

	return __xn_safe_copy_to_user(u_mx, &mx, sizeof(*u_mx));
}

int cobalt_mutex_trylock(struct cobalt_mutex_shadow __user *u_mx)
{
	struct xnthread *curr = xnshadow_current();
	struct cobalt_mutex *mutex;
	xnhandle_t handle;
	spl_t s;
	int err;

	handle = cobalt_get_handle_from_user(&u_mx->handle);

	xnlock_get_irqsave(&nklock, s);
	mutex = xnregistry_lookup(handle, NULL);
	if (!cobalt_obj_active(mutex, COBALT_MUTEX_MAGIC, typeof(*mutex))) {
		err = -EINVAL;
		goto err_unlock;
	}

	err = xnsynch_fast_acquire(mutex->synchbase.fastlock,
				   xnthread_handle(curr));
	switch(err) {
	case 0:
		if (xnthread_test_state(curr, XNWEAK))
			xnthread_inc_rescnt(curr);
		break;

/* This should not happen, as recursive mutexes are handled in
   user-space */
	case -EBUSY:
		err = -EINVAL;
		break;

	case -EAGAIN:
		err = -EBUSY;
		break;
	}
  err_unlock:
	xnlock_put_irqrestore(&nklock, s);

	return err;
}

int cobalt_mutex_lock(struct cobalt_mutex_shadow __user *u_mx)
{
	xnhandle_t handle;
	spl_t s;
	int err;

	handle = cobalt_get_handle_from_user(&u_mx->handle);

	xnlock_get_irqsave(&nklock, s);
	err = cobalt_mutex_timedlock_break(xnregistry_lookup(handle, NULL),
					   0, NULL);
	xnlock_put_irqrestore(&nklock, s);
	
	return err;
}

int cobalt_mutex_timedlock(struct cobalt_mutex_shadow __user *u_mx,
			   const struct timespec __user *u_ts)
{
	xnhandle_t handle;
	spl_t s;
	int err;

	handle = cobalt_get_handle_from_user(&u_mx->handle);

	xnlock_get_irqsave(&nklock, s);
	err = cobalt_mutex_timedlock_break(xnregistry_lookup(handle, NULL),
					   1, u_ts);
	xnlock_put_irqrestore(&nklock, s);

	return err;
}

int cobalt_mutex_unlock(struct cobalt_mutex_shadow __user *u_mx)
{
	struct cobalt_mutex *mutex;
	struct xnthread *curr;
	xnhandle_t handle;
	int err;
	spl_t s;

	handle = cobalt_get_handle_from_user(&u_mx->handle);
	curr = xnshadow_current();

	xnlock_get_irqsave(&nklock, s);
	mutex = xnregistry_lookup(handle, NULL);
	err = cobalt_mutex_release(curr, mutex);
	if (err < 0)
		goto out;

	if (err) {
		xnsched_run();
		err = 0;
	}
 out:
	xnlock_put_irqrestore(&nklock, s);

	return err;
}

void cobalt_mutexq_cleanup(struct cobalt_kqueues *q)
{
	struct cobalt_mutex *mutex, *tmp;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (list_empty(&q->mutexq))
		goto out;

	list_for_each_entry_safe(mutex, tmp, &q->mutexq, link) {
		xnlock_put_irqrestore(&nklock, s);
		cobalt_mutex_destroy_inner(mutex->handle, q);
		xnlock_get_irqsave(&nklock, s);
	}
out:
	xnlock_put_irqrestore(&nklock, s);
}

void cobalt_mutex_pkg_init(void)
{
	INIT_LIST_HEAD(&cobalt_global_kqueues.mutexq);
}

void cobalt_mutex_pkg_cleanup(void)
{
	cobalt_mutexq_cleanup(&cobalt_global_kqueues);
}

/*@}*/

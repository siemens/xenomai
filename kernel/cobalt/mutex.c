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
 * By default, Xenomai POSIX skin mutexes are of the normal type, use no
 * priority protocol and may not be shared between several processes.
 *
 * Note that only pthread_mutex_init() may be used to initialize a mutex, using
 * the static initializer @a PTHREAD_MUTEX_INITIALIZER is not supported.
 *
 *@{*/

#include <nucleus/sys_ppd.h>
#include "mutex.h"
#include "cond.h"

static int cobalt_mutex_init_inner(struct __shadow_mutex *shadow,
				   cobalt_mutex_t *mutex,
				   struct mutex_dat *datp,
				   const pthread_mutexattr_t *attr)
{
	xnflags_t synch_flags = XNSYNCH_PRIO | XNSYNCH_OWNER;
	struct xnsys_ppd *sys_ppd;
	cobalt_kqueues_t *kq;
	spl_t s;

	if (!attr)
		attr = &cobalt_default_mutex_attr;

	if (attr->magic != COBALT_MUTEX_ATTR_MAGIC)
		return -EINVAL;

	kq = cobalt_kqueues(attr->pshared);
	sys_ppd = xnsys_ppd_get(attr->pshared);

	shadow->magic = COBALT_MUTEX_MAGIC;
	shadow->mutex = mutex;
	shadow->lockcnt = 0;

	shadow->attr = *attr;
	shadow->dat_offset = xnheap_mapped_offset(&sys_ppd->sem_heap, datp);

	if (attr->protocol == PTHREAD_PRIO_INHERIT)
		synch_flags |= XNSYNCH_PIP;

	mutex->magic = COBALT_MUTEX_MAGIC;
	xnsynch_init(&mutex->synchbase, synch_flags, &datp->owner);
	datp->flags = (attr->type == PTHREAD_MUTEX_ERRORCHECK
		       ? COBALT_MUTEX_ERRORCHECK : 0);
	inith(&mutex->link);
	mutex->attr = *attr;
	mutex->owningq = kq;
	initq(&mutex->conds);

	xnlock_get_irqsave(&nklock, s);
	appendq(&kq->mutexq, &mutex->link);
	xnlock_put_irqrestore(&nklock, s);

	return 0;
}

static void cobalt_mutex_destroy_inner(cobalt_mutex_t *mutex,
				       cobalt_kqueues_t *q)
{
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	removeq(&q->mutexq, &mutex->link);
	/* synchbase wait queue may not be empty only when this function is called
	   from cobalt_mutex_pkg_cleanup, hence the absence of xnpod_schedule(). */
	xnsynch_destroy(&mutex->synchbase);
	xnlock_put_irqrestore(&nklock, s);

	xnheap_free(&xnsys_ppd_get(mutex->attr.pshared)->sem_heap,
		    mutex->synchbase.fastlock);
	xnfree(mutex);
}

static inline int cobalt_mutex_acquire(xnthread_t *cur,
				cobalt_mutex_t *mutex,
				int timed,
				xnticks_t abs_to)
{
	if (!cobalt_obj_active(mutex, COBALT_MUTEX_MAGIC, struct cobalt_mutex))
		return -EINVAL;

	if (xnsynch_owner_check(&mutex->synchbase, cur) == 0)
		return -EBUSY;

#if XENO_DEBUG(POSIX)
	if (mutex->owningq != cobalt_kqueues(mutex->attr.pshared))
		return -EPERM;
#endif /* XENO_DEBUG(POSIX) */

	return cobalt_mutex_acquire_unchecked(cur, mutex, timed, abs_to);
}

static inline int cobalt_mutex_timedlock_break(cobalt_mutex_t *mutex,
					       int timed, xnticks_t abs_to)
{
	xnthread_t *cur = xnpod_current_thread();
	spl_t s;
	int err;

	/* We need a valid thread handle for the fast lock. */
	if (xnthread_handle(cur) == XN_NO_HANDLE)
		return -EPERM;

	err = cobalt_mutex_acquire(cur, mutex, timed, abs_to);

	if (err != -EBUSY)
		goto out;

	switch(mutex->attr.type) {
	case PTHREAD_MUTEX_NORMAL:
		/* Attempting to relock a normal mutex, deadlock. */
#if XENO_DEBUG(POSIX)
		printk(KERN_WARNING
		       "POSIX: thread %s deadlocks on non-recursive mutex\n", cur->name);
#endif /* XENO_DEBUG(POSIX) */
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

		/* Recursive mutexes are handled in user-space, so
		   these cases can not happen */
	case PTHREAD_MUTEX_ERRORCHECK:
		err = -EINVAL;
		break;

	case PTHREAD_MUTEX_RECURSIVE:
		err = -EINVAL;
		break;
	}

  out:
	return err;

}

int cobalt_mutex_check_init(struct __shadow_mutex __user *u_mx)
{
	cobalt_mutex_t *mutex;
	xnholder_t *holder;
	xnqueue_t *mutexq;
	spl_t s;

	__xn_get_user(mutex, &u_mx->mutex);

	mutexq = &cobalt_kqueues(0)->mutexq;

	xnlock_get_irqsave(&nklock, s);
	for (holder = getheadq(mutexq);
	     holder; holder = nextq(mutexq, holder))
		if (holder == &mutex->link)
			goto busy;
	xnlock_put_irqrestore(&nklock, s);

	mutexq = &cobalt_kqueues(1)->mutexq;

	xnlock_get_irqsave(&nklock, s);
	for (holder = getheadq(mutexq);
	     holder; holder = nextq(mutexq, holder))
		if (holder == &mutex->link)
			goto busy;
	xnlock_put_irqrestore(&nklock, s);

	return 0;

  busy:
	xnlock_put_irqrestore(&nklock, s);
	/* mutex is already in the queue. */
	return -EBUSY;
}

int cobalt_mutex_init(struct __shadow_mutex __user *u_mx,
		      const pthread_mutexattr_t __user *u_attr)
{
	pthread_mutexattr_t locattr, *attr;
	struct __shadow_mutex mx;
	struct mutex_dat *datp;
	cobalt_mutex_t *mutex;
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

int cobalt_mutex_destroy(struct __shadow_mutex __user *u_mx)
{
	struct __shadow_mutex mx;
	cobalt_mutex_t *mutex;

	if (__xn_safe_copy_from_user(&mx, u_mx, sizeof(mx)))
		return -EFAULT;

	mutex = mx.mutex;
	if (cobalt_kqueues(mutex->attr.pshared) != mutex->owningq)
		return -EPERM;

	if (xnsynch_fast_owner_check(mutex->synchbase.fastlock,
				     XN_NO_HANDLE) != 0)
		return -EBUSY;

	if (countq(&mutex->conds))
		return -EBUSY;

	cobalt_mark_deleted(&mx);
	cobalt_mutex_destroy_inner(mutex, mutex->owningq);

	return __xn_safe_copy_to_user(u_mx, &mx, sizeof(*u_mx));
}

int cobalt_mutex_trylock(struct __shadow_mutex __user *u_mx)
{
	xnthread_t *cur = xnpod_current_thread();
	cobalt_mutex_t *mutex;
	int err;

	__xn_get_user(mutex, &u_mx->mutex);

	if (!cobalt_obj_active(mutex, COBALT_MUTEX_MAGIC,
			       struct cobalt_mutex))
		return -EINVAL;

	err = xnsynch_fast_acquire(mutex->synchbase.fastlock,
				   xnthread_handle(cur));
	switch(err) {
	case 0:
		if (xnthread_test_state(cur, XNOTHER))
			xnthread_inc_rescnt(cur);
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

	return err;
}

int cobalt_mutex_lock(struct __shadow_mutex __user *u_mx)
{
	cobalt_mutex_t *mutex;
	int err;

	__xn_get_user(mutex, &u_mx->mutex);

	err = cobalt_mutex_timedlock_break(mutex, 0, XN_INFINITE);

	return err;
}

int cobalt_mutex_timedlock(struct __shadow_mutex __user *u_mx,
			   const struct timespec __user *u_ts)
{
	cobalt_mutex_t *mutex;
	struct timespec ts;
	int err;

	__xn_get_user(mutex, &u_mx->mutex);

	if (__xn_safe_copy_from_user(&ts, u_ts, sizeof(ts)))
		return -EFAULT;

	err = cobalt_mutex_timedlock_break(mutex, 1, ts2ns(&ts) + 1);

	return err;
}

int cobalt_mutex_unlock(struct __shadow_mutex __user *u_mx)
{
	cobalt_mutex_t *mutex;
	int err;
	spl_t s;

	__xn_get_user(mutex, &u_mx->mutex);

	xnlock_get_irqsave(&nklock, s);
	err = cobalt_mutex_release(xnpod_current_thread(), mutex);
	if (err < 0)
		goto out;

	if (err) {
		xnpod_schedule();
		err = 0;
	}
  out:
	xnlock_put_irqrestore(&nklock, s);

	return err;
}

void cobalt_mutexq_cleanup(cobalt_kqueues_t *q)
{
	xnholder_t *holder;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	while ((holder = getheadq(&q->mutexq)) != NULL) {
		xnlock_put_irqrestore(&nklock, s);
		cobalt_mutex_destroy_inner(link2mutex(holder), q);
#if XENO_DEBUG(POSIX)
		xnprintf("Posix: destroying mutex %p.\n", link2mutex(holder));
#endif /* XENO_DEBUG(POSIX) */
		xnlock_get_irqsave(&nklock, s);
	}

	xnlock_put_irqrestore(&nklock, s);
}

void cobalt_mutex_pkg_init(void)
{
	initq(&cobalt_global_kqueues.mutexq);
}

void cobalt_mutex_pkg_cleanup(void)
{
	cobalt_mutexq_cleanup(&cobalt_global_kqueues);
}

/*@}*/

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
 * @defgroup cobalt_cond Condition variables services.
 *
 * Condition variables services.
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
 *@{*/

#include <cobalt/kernel/ppd.h>
#include "mutex.h"
#include "cond.h"

static inline void
cond_destroy_internal(cobalt_cond_t *cond, struct cobalt_kqueues *q)
{
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	removeq(&q->condq, &cond->link);
	/* synchbase wait queue may not be empty only when this function is
	   called from cobalt_cond_pkg_cleanup, hence the absence of
	   xnpod_schedule(). */
	xnsynch_destroy(&cond->synchbase);
	xnlock_put_irqrestore(&nklock, s);
	xnheap_free(&xnsys_ppd_get(cond->attr.pshared)->sem_heap,
		    cond->pending_signals);
	xnfree(cond);
}

/**
 * Initialize a condition variable.
 *
 * This service initializes the condition variable @a cnd, using the condition
 * variable attributes object @a attr. If @a attr is @a NULL or this service is
 * called from user-space, default attributes are used (see
 * pthread_condattr_init()).
 *
 * @param cnd the condition variable to be initialized;
 *
 * @param attr the condition variable attributes object.
 *
 * @return 0 on succes,
 * @return an error number if:
 * - EINVAL, the condition variable attributes object @a attr is invalid or
 *   uninitialized;
 * - EBUSY, the condition variable @a cnd was already initialized;
 * - ENOMEM, insufficient memory exists in the system heap to initialize the
 *   condition variable, increase CONFIG_XENO_OPT_SYS_HEAPSZ.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_cond_init.html">
 * Specification.</a>
 *
 */
static inline int
pthread_cond_init(struct __shadow_cond *cnd, const pthread_condattr_t *attr)
{
	xnflags_t synch_flags = XNSYNCH_PRIO | XNSYNCH_NOPIP;
	struct xnsys_ppd *sys_ppd;
	struct xnholder *holder;
	cobalt_cond_t *cond;
	xnqueue_t *condq;
	spl_t s;
	int err;

	if (!attr)
		attr = &cobalt_default_cond_attr;

	cond = (cobalt_cond_t *)xnmalloc(sizeof(*cond));
	if (!cond)
		return -ENOMEM;

	sys_ppd = xnsys_ppd_get(attr->pshared);
	cond->pending_signals = (unsigned long *)
		xnheap_alloc(&sys_ppd->sem_heap,
			     sizeof(*(cond->pending_signals)));
	if (!cond->pending_signals) {
		err = -EAGAIN;
		goto err_free_cond;
	}
	*(cond->pending_signals) = 0;

	xnlock_get_irqsave(&nklock, s);

	if (attr->magic != COBALT_COND_ATTR_MAGIC) {
		err = -EINVAL;
		goto err_free_pending_signals;
	}

	condq = &cobalt_kqueues(attr->pshared)->condq;

	/*
	 * We allow reinitializing a shared condvar. Rationale: since
	 * a condvar is inherently anonymous, if the process creating
	 * such condvar exits, we may assume that other processes
	 * sharing that condvar won't be able to keep on running.
	 */
	if (cnd->magic == COBALT_COND_MAGIC) {
		for (holder = getheadq(condq); holder;
		     holder = nextq(condq, holder))
			if (holder == &cnd->cond->link) {
				if (attr->pshared) {
					cond_destroy_internal(cnd->cond,
							      cobalt_kqueues(1));
					goto do_init;
				}
				err = -EBUSY;
				goto err_free_pending_signals;
			}
	}
do_init:
	cnd->attr = *attr;
	cnd->pending_signals_offset =
		xnheap_mapped_offset(&sys_ppd->sem_heap,
				     cond->pending_signals);
	cnd->mutex_datp = (struct mutex_dat *)~0UL;

	cnd->magic = COBALT_COND_MAGIC;
	cnd->cond = cond;

	cond->magic = COBALT_COND_MAGIC;
	xnsynch_init(&cond->synchbase, synch_flags, NULL);
	inith(&cond->link);
	cond->attr = *attr;
	cond->mutex = NULL;
	cond->owningq = cobalt_kqueues(attr->pshared);

	appendq(condq, &cond->link);

	xnlock_put_irqrestore(&nklock, s);

	return 0;

  err_free_pending_signals:
	xnlock_put_irqrestore(&nklock, s);
	xnheap_free(&xnsys_ppd_get(cond->attr.pshared)->sem_heap,
		    cond->pending_signals);
  err_free_cond:
	xnfree(cond);
	return err;
}

/**
 * Destroy a condition variable.
 *
 * This service destroys the condition variable @a cnd, if no thread is
 * currently blocked on it. The condition variable becomes invalid for all
 * condition variable services (they all return the EINVAL error) except
 * pthread_cond_init().
 *
 * @param cnd the condition variable to be destroyed.
 *
 * @return 0 on succes,
 * @return an error number if:
 * - EINVAL, the condition variable @a cnd is invalid;
 * - EPERM, the condition variable is not process-shared and does not belong to
 *   the current process;
 * - EBUSY, some thread is currently using the condition variable.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_cond_destroy.html">
 * Specification.</a>
 *
 */
static inline int pthread_cond_destroy(struct __shadow_cond *cnd)
{
	cobalt_cond_t *cond;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	cond = cnd->cond;
	if (!cobalt_obj_active(cnd, COBALT_COND_MAGIC, struct __shadow_cond)
	    || !cobalt_obj_active(cond, COBALT_COND_MAGIC, struct cobalt_cond)) {
		xnlock_put_irqrestore(&nklock, s);
		return -EINVAL;
	}

	if (cond->owningq != cobalt_kqueues(cond->attr.pshared)) {
		xnlock_put_irqrestore(&nklock, s);
		return -EPERM;
	}

	if (xnsynch_nsleepers(&cond->synchbase) || cond->mutex) {
		xnlock_put_irqrestore(&nklock, s);
		return -EBUSY;
	}

	cobalt_mark_deleted(cnd);
	cobalt_mark_deleted(cond);

	xnlock_put_irqrestore(&nklock, s);

	cond_destroy_internal(cond, cobalt_kqueues(cond->attr.pshared));

	return 0;
}

static inline int cobalt_cond_timedwait_prologue(xnthread_t *cur,
						 cobalt_cond_t *cond,
						 cobalt_mutex_t *mutex,
						 int timed,
						 xnticks_t abs_to)
{
	spl_t s;
	int err;

	xnlock_get_irqsave(&nklock, s);

	/* If another thread waiting for cond does not use the same mutex */
	if (!cobalt_obj_active(cond, COBALT_COND_MAGIC, struct cobalt_cond)
	    || (cond->mutex && cond->mutex != mutex)) {
		err = -EINVAL;
		goto unlock_and_return;
	}

#if XENO_DEBUG(NUCLEUS)
	if (cond->owningq != cobalt_kqueues(cond->attr.pshared)) {
		err = -EPERM;
		goto unlock_and_return;
	}
#endif

	if (mutex->attr.pshared != cond->attr.pshared) {
		err = -EINVAL;
		goto unlock_and_return;
	}

	/* Unlock mutex. */
	err = cobalt_mutex_release(cur, mutex);
	if (err < 0)
		goto unlock_and_return;

	/* err == 1 means a reschedule is needed, but do not
	   reschedule here, releasing the mutex and suspension must be
	   done atomically in pthread_cond_*wait. */

	/* Bind mutex to cond. */
	if (cond->mutex == NULL) {
		cond->mutex = mutex;
		inith(&cond->mutex_link);
		appendq(&mutex->conds, &cond->mutex_link);
	}

	/* Wait for another thread to signal the condition. */
	if (timed)
		xnsynch_sleep_on(&cond->synchbase, abs_to,
				 clock_flag(TIMER_ABSTIME, cond->attr.clock));
	else
		xnsynch_sleep_on(&cond->synchbase, XN_INFINITE, XN_RELATIVE);

	/* There are four possible wakeup conditions :
	   - cond_signal / cond_broadcast, no status bit is set, and the function
	     should return 0 ;
	   - timeout, the status XNTIMEO is set, and the function should return
	     ETIMEDOUT ;
	   - pthread_kill, the status bit XNBREAK is set, but ignored, the
	     function simply returns EINTR (used only by the user-space
	     interface, replaced by 0 anywhere else), causing a wakeup, spurious
	     or not whether pthread_cond_signal was called between pthread_kill
	     and the moment when xnsynch_sleep_on returned ;
	   - pthread_cancel, no status bit is set, but cancellation specific
	     bits are set, and tested only once the mutex is reacquired in
	     cobalt_cond_timedwait_epilogue, so that the cancellation handler can
	     be called with the mutex locked, as required by the specification.
	 */

	err = 0;

	if (xnthread_test_info(cur, XNBREAK))
		err = -EINTR;
	else if (xnthread_test_info(cur, XNTIMEO))
		err = -ETIMEDOUT;

      unlock_and_return:
	xnlock_put_irqrestore(&nklock, s);

	return err;
}

static inline int cobalt_cond_timedwait_epilogue(xnthread_t *cur,
						 cobalt_cond_t *cond,
						 cobalt_mutex_t *mutex)
{
	int err;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	err = cobalt_mutex_acquire_unchecked(cur, mutex, 0, XN_INFINITE);
	if (err == -EINTR)
		goto unlock_and_return;

	/* Unbind mutex and cond, if no other thread is waiting, if the job was
	   not already done. */
	if (!xnsynch_nsleepers(&cond->synchbase)
	    && cond->mutex == mutex) {
		cond->mutex = NULL;
		removeq(&mutex->conds, &cond->mutex_link);
	}

      unlock_and_return:
	xnlock_put_irqrestore(&nklock, s);

	return err;
}

int cobalt_cond_init(struct __shadow_cond __user *u_cnd,
		     const pthread_condattr_t __user *u_attr)
{
	pthread_condattr_t locattr, *attr;
	struct __shadow_cond cnd;
	int err;

	if (__xn_safe_copy_from_user(&cnd, u_cnd, sizeof(cnd)))
		return -EFAULT;

	if (u_attr) {
		if (__xn_safe_copy_from_user(&locattr,
					     u_attr, sizeof(locattr)))
			return -EFAULT;

		attr = &locattr;
	} else
		attr = NULL;

	/* Always use default attribute. */
	err = pthread_cond_init(&cnd, attr);
	if (err < 0)
		return err;

	return __xn_safe_copy_to_user(u_cnd, &cnd, sizeof(*u_cnd));
}

int cobalt_cond_destroy(struct __shadow_cond __user *u_cnd)
{
	struct __shadow_cond cnd;
	int err;

	if (__xn_safe_copy_from_user(&cnd, u_cnd, sizeof(cnd)))
		return -EFAULT;

	err = pthread_cond_destroy(&cnd);
	if (err < 0)
		return err;

	return __xn_safe_copy_to_user(u_cnd, &cnd, sizeof(*u_cnd));
}

struct us_cond_data {
	int err;
};

/* pthread_cond_wait_prologue(cond, mutex, count_ptr, timed, timeout) */
int cobalt_cond_wait_prologue(struct __shadow_cond __user *u_cnd,
			      struct __shadow_mutex __user *u_mx,
			      int *u_err,
			      unsigned int timed,
			      struct timespec __user *u_ts)
{
	xnthread_t *cur = xnshadow_current();
	struct mutex_dat *datp;
	struct us_cond_data d;
	cobalt_cond_t *cnd;
	cobalt_mutex_t *mx;
	struct timespec ts;
	int err, perr = 0;

	__xn_get_user(cnd, &u_cnd->cond);
	__xn_get_user(mx, &u_mx->mutex);

	if (!cnd->mutex) {
		__xn_get_user(datp, &u_mx->dat);
		__xn_put_user(datp, &u_cnd->mutex_datp);
	}

	if (timed) {
		err = __xn_safe_copy_from_user(&ts, u_ts, sizeof(ts))?-EFAULT:0;
		if (!err)
			err = cobalt_cond_timedwait_prologue(cur,
							     cnd, mx, timed,
							     ts2ns(&ts) + 1);
	} else
		err = cobalt_cond_timedwait_prologue(cur, cnd,
						     mx, timed,
						     XN_INFINITE);

	switch(err) {
	case 0:
	case -ETIMEDOUT:
		perr = d.err = err;
		err = cobalt_cond_timedwait_epilogue(cur, cnd, mx);
		break;

	case -EINTR:
		perr = err;
		d.err = 0;	/* epilogue should return 0. */
		break;

	default:
		/* Please gcc and handle the case which will never
		   happen */
		d.err = EINVAL;
	}

	if (!cnd->mutex) {
		datp = (struct mutex_dat *)~0UL;
		__xn_put_user(datp, &u_cnd->mutex_datp);
	}

	if (err == -EINTR)
		__xn_put_user(d.err, u_err);

	return err == 0 ? perr : err;
}

int cobalt_cond_wait_epilogue(struct __shadow_cond __user *u_cnd,
			      struct __shadow_mutex __user *u_mx)
{
	xnthread_t *cur = xnshadow_current();
	cobalt_cond_t *cnd;
	cobalt_mutex_t *mx;
	int err;

	__xn_get_user(cnd, &u_cnd->cond);
	__xn_get_user(mx, &u_mx->mutex);

	err = cobalt_cond_timedwait_epilogue(cur, cnd, mx);

	if (!cnd->mutex) {
		struct mutex_dat *datp = (struct mutex_dat *)~0UL;
		__xn_put_user(datp, &u_cnd->mutex_datp);
	}

	return err;
}

void cobalt_condq_cleanup(struct cobalt_kqueues *q)
{
	xnholder_t *holder;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	while ((holder = getheadq(&q->condq)) != NULL) {
		xnlock_put_irqrestore(&nklock, s);
		cond_destroy_internal(link2cond(holder), q);
#if XENO_DEBUG(COBALT)
		printk(XENO_INFO "deleting Cobalt condvar %p\n",
			 link2cond(holder));
#endif /* XENO_DEBUG(COBALT) */
		xnlock_get_irqsave(&nklock, s);
	}

	xnlock_put_irqrestore(&nklock, s);
}

void cobalt_cond_pkg_init(void)
{
	initq(&cobalt_global_kqueues.condq);
}

void cobalt_cond_pkg_cleanup(void)
{
	cobalt_condq_cleanup(&cobalt_global_kqueues);
}

/*@}*/

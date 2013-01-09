/*!\file synch.c
 * \brief Thread synchronization services.
 * \author Philippe Gerum
 *
 * Copyright (C) 2001-2008 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * \ingroup synch
 */

/*!
 * \ingroup nucleus
 * \defgroup synch Thread synchronization services.
 *
 * Thread synchronization services.
 *
 *@{*/

#include <stdarg.h>
#include <nucleus/pod.h>
#include <nucleus/synch.h>
#include <nucleus/thread.h>
#include <nucleus/module.h>

#define w_bprio(t)	xnsched_weighted_bprio(t)
#define w_cprio(t)	xnsched_weighted_cprio(t)

/*!
 * \fn void xnsynch_init(struct xnsynch *synch, xnflags_t flags,
 *                       xnarch_atomic_t *fastlock)
 *
 * \brief Initialize a synchronization object.
 *
 * Initializes a new specialized object which can subsequently be used
 * to synchronize real-time activities. The Xenomai nucleus
 * provides a basic synchronization object which can be used to build
 * higher resource objects. Nucleus threads can wait for and signal
 * such objects in order to synchronize their activities.
 *
 * This object has built-in support for priority inheritance.
 *
 * @param synch The address of a synchronization object descriptor the
 * nucleus will use to store the object-specific data.  This
 * descriptor must always be valid while the object is active
 * therefore it must be allocated in permanent memory.
 *
 * @param flags A set of creation flags affecting the operation. The
 * valid flags are:
 *
 * - XNSYNCH_PRIO causes the threads waiting for the resource to pend
 * in priority order. Otherwise, FIFO ordering is used (XNSYNCH_FIFO).
 *
 * - XNSYNCH_OWNER indicates that the synchronization object shall
 * track its owning thread (required if XNSYNCH_PIP is selected). Note
 * that setting this flag implies the use xnsynch_acquire and
 * xnsynch_release instead of xnsynch_sleep_on and
 * xnsynch_wakeup_one_sleeper/xnsynch_wakeup_this_sleeper.
 *
 * - XNSYNCH_PIP causes the priority inheritance mechanism to be
 * automatically activated when a priority inversion is detected among
 * threads using this object. Otherwise, no priority inheritance takes
 * place upon priority inversion (XNSYNCH_NOPIP).
 *
 * - XNSYNCH_DREORD (Disable REORDering) tells the nucleus that the
 * wait queue should not be reordered whenever the priority of a
 * blocked thread it holds is changed. If this flag is not specified,
 * changing the priority of a blocked thread using
 * xnpod_set_thread_schedparam() will cause this object's wait queue
 * to be reordered according to the new priority level, provided the
 * synchronization object makes the waiters wait by priority order on
 * the awaited resource (XNSYNCH_PRIO).
 *
 * @param fastlock Address of the fast lock word to be associated with
 * the synchronization object. If NULL is passed or XNSYNCH_OWNER is not
 * set, fast-lock support is disabled.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

void xnsynch_init(struct xnsynch *synch, xnflags_t flags, xnarch_atomic_t *fastlock)
{
	initph(&synch->link);

	if (flags & XNSYNCH_PIP)
		flags |= XNSYNCH_PRIO | XNSYNCH_OWNER;	/* Obviously... */

	synch->status = flags & ~XNSYNCH_CLAIMED;
	synch->owner = NULL;
	synch->cleanup = NULL;	/* Only works for PIP-enabled objects. */
#ifdef CONFIG_XENO_FASTSYNCH
	if ((flags & XNSYNCH_OWNER) && fastlock) {
		synch->fastlock = fastlock;
		xnarch_atomic_set(fastlock, XN_NO_HANDLE);
	} else
		synch->fastlock = NULL;
#endif /* CONFIG_XENO_FASTSYNCH */
	initpq(&synch->pendq);
	xnarch_init_display_context(synch);
}
EXPORT_SYMBOL_GPL(xnsynch_init);

/*!
 * \fn xnflags_t xnsynch_sleep_on(struct xnsynch *synch, xnticks_t timeout,
 *                                xntmode_t timeout_mode);
 * \brief Sleep on an ownerless synchronization object.
 *
 * Makes the calling thread sleep on the specified synchronization
 * object, waiting for it to be signaled.
 *
 * This service should be called by upper interfaces wanting the
 * current thread to pend on the given resource. It must not be used
 * with synchronization objects that are supposed to track ownership
 * (XNSYNCH_OWNER).
 *
 * @param synch The descriptor address of the synchronization object
 * to sleep on.
 *
 * @param timeout The timeout which may be used to limit the time the
 * thread pends on the resource. This value is a wait time given in
 * ticks (see note). It can either be relative, absolute monotonic, or
 * absolute adjustable depending on @a timeout_mode. Passing XN_INFINITE
 * @b and setting @a mode to XN_RELATIVE specifies an unbounded wait. All
 * other values are used to initialize a watchdog timer.
 *
 * @param timeout_mode The mode of the @a timeout parameter. It can
 * either be set to XN_RELATIVE, XN_ABSOLUTE, or XN_REALTIME (see also
 * xntimer_start()).
 *
 * @return A bitmask which may include zero or one information bit
 * among XNRMID, XNTIMEO and XNBREAK, which should be tested by the
 * caller, for detecting respectively: object deletion, timeout or
 * signal/unblock conditions which might have happened while waiting.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: always.
 *
 * @note The @a timeout value will be interpreted as jiffies if the
 * current thread is bound to a periodic time base (see
 * xnpod_init_thread), or nanoseconds otherwise.
 */

xnflags_t xnsynch_sleep_on(struct xnsynch *synch, xnticks_t timeout,
			   xntmode_t timeout_mode)
{
	struct xnthread *thread = xnpod_current_thread();
	spl_t s;

	XENO_BUGON(NUCLEUS, testbits(synch->status, XNSYNCH_OWNER));

	xnlock_get_irqsave(&nklock, s);

	trace_mark(xn_nucleus, synch_sleepon,
		   "thread %p thread_name %s synch %p",
		   thread, xnthread_name(thread), synch);

	if (!testbits(synch->status, XNSYNCH_PRIO)) /* i.e. FIFO */
		appendpq(&synch->pendq, &thread->plink);
	else /* i.e. priority-sorted */
		insertpqf(&synch->pendq, &thread->plink, w_cprio(thread));

	xnpod_suspend_thread(thread, XNPEND, timeout, timeout_mode, synch);

	xnlock_put_irqrestore(&nklock, s);

	return xnthread_test_info(thread, XNRMID|XNTIMEO|XNBREAK);
}
EXPORT_SYMBOL_GPL(xnsynch_sleep_on);

/*!
 * \fn struct xnthread *xnsynch_wakeup_one_sleeper(struct xnsynch *synch);
 * \brief Give the resource ownership to the next waiting thread.
 *
 * This service wakes up the thread which is currently leading the
 * synchronization object's pending list. The sleeping thread is
 * unblocked from its pending state, but no reschedule is performed.
 *
 * This service should be called by upper interfaces wanting to signal
 * the given resource so that a single waiter is resumed. It must not
 * be used with synchronization objects that are supposed to track
 * ownership (XNSYNCH_OWNER not set).
 *
 * @param synch The descriptor address of the synchronization object
 * whose ownership is changed.
 *
 * @return The descriptor address of the unblocked thread.
 *
 * Side-effects:
 *
 * - The effective priority of the previous resource owner might be
 * lowered to its base priority value as a consequence of the priority
 * inheritance boost being cleared.
 *
 * - The synchronization object ownership is transfered to the
 * unblocked thread.
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

struct xnthread *xnsynch_wakeup_one_sleeper(struct xnsynch *synch)
{
	struct xnthread *thread = NULL;
	struct xnpholder *holder;
	spl_t s;

	XENO_BUGON(NUCLEUS, testbits(synch->status, XNSYNCH_OWNER));

	xnlock_get_irqsave(&nklock, s);

	holder = getpq(&synch->pendq);
	if (holder) {
		thread = link2thread(holder, plink);
		thread->wchan = NULL;
		trace_mark(xn_nucleus, synch_wakeup_one,
			   "thread %p thread_name %s synch %p",
			   thread, xnthread_name(thread), synch);
		xnpod_resume_thread(thread, XNPEND);
	}

	xnlock_put_irqrestore(&nklock, s);

	xnarch_post_graph_if(synch, 0, emptypq_p(&synch->pendq));

	return thread;
}
EXPORT_SYMBOL_GPL(xnsynch_wakeup_one_sleeper);

/*!
 * \fn void xnsynch_wakeup_this_sleeper(struct xnsynch *synch, struct xnpholder *holder);
 * \brief Give the resource ownership to a given waiting thread.
 *
 * This service wakes up a specific thread which is currently pending on
 * the given synchronization object. The sleeping thread is unblocked
 * from its pending state, but no reschedule is performed.
 *
 * This service should be called by upper interfaces wanting to signal
 * the given resource so that a specific waiter is resumed. It must not
 * be used with synchronization objects that are supposed to track
 * ownership (XNSYNCH_OWNER not set).
 *
 * @param synch The descriptor address of the synchronization object
 * whose ownership is changed.
 *
 * @param holder The link holder address of the thread to unblock
 * (&thread->plink) which MUST be currently linked to the
 * synchronization object's pending queue (i.e. synch->pendq).
 *
 * @return The link address of the unblocked thread in the
 * synchronization object's pending queue.
 *
 * Side-effects:
 *
 * - The effective priority of the previous resource owner might be
 * lowered to its base priority value as a consequence of the priority
 * inheritance boost being cleared.
 *
 * - The synchronization object ownership is transfered to the
 * unblocked thread.
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

struct xnpholder *xnsynch_wakeup_this_sleeper(struct xnsynch *synch, struct xnpholder *holder)
{
	struct xnthread *thread;
	struct xnpholder *nholder;
	spl_t s;

	XENO_BUGON(NUCLEUS, testbits(synch->status, XNSYNCH_OWNER));

	xnlock_get_irqsave(&nklock, s);

	nholder = poppq(&synch->pendq, holder);
	thread = link2thread(holder, plink);
	thread->wchan = NULL;
	trace_mark(xn_nucleus, synch_wakeup_this,
		   "thread %p thread_name %s synch %p",
		   thread, xnthread_name(thread), synch);
	xnpod_resume_thread(thread, XNPEND);

	xnlock_put_irqrestore(&nklock, s);

	xnarch_post_graph_if(synch, 0, emptypq_p(&synch->pendq));

	return nholder;
}
EXPORT_SYMBOL_GPL(xnsynch_wakeup_this_sleeper);

/*
 * xnsynch_renice_thread() -- This service is used by the PIP code to
 * raise/lower a thread's priority. The thread's base priority value
 * is _not_ changed and if ready, the thread is always moved at the
 * end of its priority group.
 */

static void xnsynch_renice_thread(struct xnthread *thread,
				  struct xnthread *target)
{
	/* Apply the scheduling policy of "target" to "thread" */
	xnsched_track_policy(thread, target);

	if (thread->wchan)
		xnsynch_requeue_sleeper(thread);

#ifdef CONFIG_XENO_OPT_PERVASIVE
	if (xnthread_test_state(thread, XNRELAX))
		xnshadow_renice(thread);
	else if (xnthread_test_state(thread, XNSHADOW))
		xnthread_set_info(thread, XNPRIOSET);
#endif /* CONFIG_XENO_OPT_PERVASIVE */
}

/*!
 * \fn xnflags_t xnsynch_acquire(struct xnsynch *synch, xnticks_t timeout,
 *                               xntmode_t timeout_mode);
 * \brief Acquire the ownership of a synchronization object.
 *
 * This service should be called by upper interfaces wanting the
 * current thread to acquire the ownership of the given resource. If
 * the resource is already assigned to a thread, the caller is
 * suspended.
 *
 * This service must be used only with synchronization objects that
 * track ownership (XNSYNCH_OWNER set.
 *
 * @param synch The descriptor address of the synchronization object
 * to acquire.
 *
 * @param timeout The timeout which may be used to limit the time the
 * thread pends on the resource. This value is a wait time given in
 * ticks (see note). It can either be relative, absolute monotonic, or
 * absolute adjustable depending on @a timeout_mode. Passing XN_INFINITE
 * @b and setting @a mode to XN_RELATIVE specifies an unbounded wait. All
 * other values are used to initialize a watchdog timer.
 *
 * @param timeout_mode The mode of the @a timeout parameter. It can
 * either be set to XN_RELATIVE, XN_ABSOLUTE, or XN_REALTIME (see also
 * xntimer_start()).
 *
 * @return A bitmask which may include zero or one information bit
 * among XNRMID, XNTIMEO and XNBREAK, which should be tested by the
 * caller, for detecting respectively: object deletion, timeout or
 * signal/unblock conditions which might have happened while waiting.
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
 *
 * @note The @a timeout value will be interpreted as jiffies if the
 * current thread is bound to a periodic time base (see
 * xnpod_init_thread), or nanoseconds otherwise.
 */

xnflags_t xnsynch_acquire(struct xnsynch *synch, xnticks_t timeout,
			  xntmode_t timeout_mode)
{
	struct xnthread *thread = xnpod_current_thread(), *owner;
	xnhandle_t threadh = xnthread_handle(thread), fastlock, old;
	const int use_fastlock = xnsynch_fastlock_p(synch);
	spl_t s;

	XENO_BUGON(NUCLEUS, !testbits(synch->status, XNSYNCH_OWNER));

	trace_mark(xn_nucleus, synch_acquire, "synch %p", synch);

      redo:

	if (use_fastlock) {
		xnarch_atomic_t *lockp = xnsynch_fastlock(synch);

		fastlock = xnarch_atomic_cmpxchg(lockp,
						 XN_NO_HANDLE, threadh);

		if (likely(fastlock == XN_NO_HANDLE)) {
			if (xnthread_test_state(thread, XNOTHER))
				xnthread_inc_rescnt(thread);
			xnthread_clear_info(thread,
					    XNRMID | XNTIMEO | XNBREAK);
			return 0;
		}

		xnlock_get_irqsave(&nklock, s);

		/* Set claimed bit.
		   In case it appears to be set already, re-read its state
		   under nklock so that we don't miss any change between the
		   lock-less read and here. But also try to avoid cmpxchg
		   where possible. Only if it appears not to be set, start
		   with cmpxchg directly. */
		if (xnsynch_fast_is_claimed(fastlock)) {
			old = xnarch_atomic_get(lockp);
			goto test_no_owner;
		}
		do {
			old = xnarch_atomic_cmpxchg(lockp, fastlock,
					xnsynch_fast_set_claimed(fastlock, 1));
			if (likely(old == fastlock))
				break;

		  test_no_owner:
			if (old == XN_NO_HANDLE) {
				/* Owner called xnsynch_release
				   (on another cpu) */
				xnlock_put_irqrestore(&nklock, s);
				goto redo;
			}
			fastlock = old;
		} while (!xnsynch_fast_is_claimed(fastlock));

		owner = xnthread_lookup(xnsynch_fast_mask_claimed(fastlock));

		if (!owner) {
			/* The handle is broken, therefore pretend that the synch
			   object was deleted to signal an error. */
			xnthread_set_info(thread, XNRMID);
			goto unlock_and_exit;
		}

		xnsynch_set_owner(synch, owner);
	} else {
		xnlock_get_irqsave(&nklock, s);

		owner = synch->owner;

		if (!owner) {
			synch->owner = thread;
			if (xnthread_test_state(thread, XNOTHER))
				xnthread_inc_rescnt(thread);
			xnthread_clear_info(thread,
					    XNRMID | XNTIMEO | XNBREAK);
			goto unlock_and_exit;
		}
	}

	xnsynch_detect_relaxed_owner(synch, thread);

	if (!testbits(synch->status, XNSYNCH_PRIO)) /* i.e. FIFO */
		appendpq(&synch->pendq, &thread->plink);
	else if (w_cprio(thread) > w_cprio(owner)) {
		if (xnthread_test_info(owner, XNWAKEN) && owner->wwake == synch) {
			/* Ownership is still pending, steal the resource. */
			synch->owner = thread;
			xnthread_clear_info(thread, XNRMID | XNTIMEO | XNBREAK);
			xnthread_set_info(owner, XNROBBED);
			goto grab_and_exit;
		}

		insertpqf(&synch->pendq, &thread->plink, w_cprio(thread));

		if (testbits(synch->status, XNSYNCH_PIP)) {
			if (!xnthread_test_state(owner, XNBOOST)) {
				owner->bprio = owner->cprio;
				xnthread_set_state(owner, XNBOOST);
			}

			if (testbits(synch->status, XNSYNCH_CLAIMED))
				removepq(&owner->claimq, &synch->link);
			else
				__setbits(synch->status, XNSYNCH_CLAIMED);

			insertpqf(&owner->claimq, &synch->link, w_cprio(thread));
			xnsynch_renice_thread(owner, thread);
		}
	} else
		insertpqf(&synch->pendq, &thread->plink, w_cprio(thread));

	xnpod_suspend_thread(thread, XNPEND, timeout, timeout_mode, synch);

	thread->wwake = NULL;
	xnthread_clear_info(thread, XNWAKEN);

	if (xnthread_test_info(thread, XNRMID | XNTIMEO | XNBREAK))
		goto unlock_and_exit;

	if (xnthread_test_info(thread, XNROBBED)) {
		/* Somebody stole us the ownership while we were ready
		   to run, waiting for the CPU: we need to wait again
		   for the resource. */
		if (timeout_mode != XN_RELATIVE || timeout == XN_INFINITE) {
			xnlock_put_irqrestore(&nklock, s);
			goto redo;
		}
		timeout = xntimer_get_timeout_stopped(&thread->rtimer);
		if (timeout > 1) { /* Otherwise, it's too late. */
			xnlock_put_irqrestore(&nklock, s);
			goto redo;
		}
		xnthread_set_info(thread, XNTIMEO);
	} else {

	      grab_and_exit:

		if (xnthread_test_state(thread, XNOTHER))
			xnthread_inc_rescnt(thread);

		if (use_fastlock) {
			xnarch_atomic_t *lockp = xnsynch_fastlock(synch);
			/* We are the new owner, update the fastlock
			   accordingly. */
			if (xnsynch_pended_p(synch))
				threadh =
				    xnsynch_fast_set_claimed(threadh, 1);
			xnarch_atomic_set(lockp, threadh);
		}
	}

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return xnthread_test_info(thread, XNRMID|XNTIMEO|XNBREAK);
}
EXPORT_SYMBOL_GPL(xnsynch_acquire);

/*!
 * @internal
 * \fn void xnsynch_clear_boost(struct xnsynch *synch, struct xnthread *owner);
 * \brief Clear the priority boost.
 *
 * This service is called internally whenever a synchronization object
 * is not claimed anymore by sleepers to reset the object owner's
 * priority to its initial level.
 *
 * @param synch The descriptor address of the synchronization object.
 *
 * @param owner The descriptor address of the thread which
 * currently owns the synchronization object.
 *
 * @note This routine must be entered nklock locked, interrupts off.
 */

static void xnsynch_clear_boost(struct xnsynch *synch,
				struct xnthread *owner)
{
	struct xnthread *target;
	struct xnsynch *hsynch;
	struct xnpholder *h;
	int wprio;

	removepq(&owner->claimq, &synch->link);
	__clrbits(synch->status, XNSYNCH_CLAIMED);
	wprio = w_bprio(owner);

	if (emptypq_p(&owner->claimq)) {
		xnthread_clear_state(owner, XNBOOST);
		target = owner;
	} else {
		/* Find the highest priority needed to enforce the PIP. */
		hsynch = link2synch(getheadpq(&owner->claimq));
		h = getheadpq(&hsynch->pendq);
		XENO_BUGON(NUCLEUS, h == NULL);
		target = link2thread(h, plink);
		if (w_cprio(target) > wprio)
			wprio = w_cprio(target);
		else
			target = owner;
	}

	if (w_cprio(owner) != wprio &&
	    !xnthread_test_state(owner, XNZOMBIE))
		xnsynch_renice_thread(owner, target);
}

/*!
 * @internal
 * \fn void xnsynch_requeue_sleeper(struct xnthread *thread);
 * \brief Change a sleeper's priority.
 *
 * This service is used by the PIP code to update the pending priority
 * of a sleeping thread.
 *
 * @param thread The descriptor address of the affected thread.
 *
 * @note This routine must be entered nklock locked, interrupts off.
 */

void xnsynch_requeue_sleeper(struct xnthread *thread)
{
	struct xnsynch *synch = thread->wchan;
	struct xnthread *owner;

	if (!testbits(synch->status, XNSYNCH_PRIO))
		return;

	removepq(&synch->pendq, &thread->plink);
	insertpqf(&synch->pendq, &thread->plink, w_cprio(thread));
	owner = synch->owner;

	if (owner != NULL && w_cprio(thread) > w_cprio(owner)) {
		/*
		 * The new (weighted) priority of the sleeping thread
		 * is higher than the priority of the current owner of
		 * the resource: we need to update the PI state.
		 */
		if (testbits(synch->status, XNSYNCH_CLAIMED)) {
			/*
			 * The resource is already claimed, just
			 * reorder the claim queue.
			 */
			removepq(&owner->claimq, &synch->link);
			insertpqf(&owner->claimq, &synch->link,
				  w_cprio(thread));
		} else {
			/*
			 * The resource was NOT claimed, claim it now
			 * and boost the owner.
			 */
			__setbits(synch->status, XNSYNCH_CLAIMED);
			insertpqf(&owner->claimq, &synch->link,
				  w_cprio(thread));
			if (!xnthread_test_state(owner, XNBOOST)) {
				owner->bprio = owner->cprio;
				xnthread_set_state(owner, XNBOOST);
			}
		}
		/*
		 * Renice the owner thread, progressing in the PI
		 * chain as needed.
		 */
		xnsynch_renice_thread(owner, thread);
	}
}
EXPORT_SYMBOL_GPL(xnsynch_requeue_sleeper);

static struct xnthread *
xnsynch_release_thread(struct xnsynch *synch, struct xnthread *lastowner)
{
	const int use_fastlock = xnsynch_fastlock_p(synch);
	xnhandle_t lastownerh, newownerh;
	struct xnthread *newowner;
	struct xnpholder *holder;
	spl_t s;

	XENO_BUGON(NUCLEUS, !testbits(synch->status, XNSYNCH_OWNER));

#ifdef CONFIG_XENO_OPT_PERVASIVE
	if (xnthread_test_state(lastowner, XNOTHER)) {
		if (xnthread_get_rescnt(lastowner) == 0)
			xnshadow_send_sig(lastowner, SIGDEBUG,
					  SIGDEBUG_RESCNT_IMBALANCE, 1);
		else
			xnthread_dec_rescnt(lastowner);
	}
#endif
	lastownerh = xnthread_handle(lastowner);

	if (use_fastlock &&
	    likely(xnsynch_fast_release(xnsynch_fastlock(synch), lastownerh)))
		return NULL;

	xnlock_get_irqsave(&nklock, s);

	trace_mark(xn_nucleus, synch_release, "synch %p", synch);

	holder = getpq(&synch->pendq);
	if (holder) {
		newowner = link2thread(holder, plink);
		newowner->wchan = NULL;
		newowner->wwake = synch;
		synch->owner = newowner;
		xnthread_set_info(newowner, XNWAKEN);
		xnpod_resume_thread(newowner, XNPEND);

		if (testbits(synch->status, XNSYNCH_CLAIMED))
			xnsynch_clear_boost(synch, lastowner);

		newownerh = xnsynch_fast_set_claimed(xnthread_handle(newowner),
						     xnsynch_pended_p(synch));
	} else {
		newowner = NULL;
		synch->owner = NULL;
		newownerh = XN_NO_HANDLE;
	}
	if (use_fastlock) {
		xnarch_atomic_t *lockp = xnsynch_fastlock(synch);
		xnarch_atomic_set(lockp, newownerh);
	}

	xnlock_put_irqrestore(&nklock, s);

	xnarch_post_graph_if(synch, 0, emptypq_p(&synch->pendq));

	return newowner;
}

/*!
 * \fn struct xnthread *xnsynch_release(struct xnsynch *synch);
 * \brief Give the resource ownership to the next waiting thread.
 *
 * This service releases the ownership of the given synchronization
 * object. The thread which is currently leading the object's pending
 * list, if any, is unblocked from its pending state. However, no
 * reschedule is performed.
 *
 * This service must be used only with synchronization objects that
 * track ownership (XNSYNCH_OWNER set).
 *
 * @param synch The descriptor address of the synchronization object
 * whose ownership is changed.
 *
 * @return The descriptor address of the unblocked thread.
 *
 * Side-effects:
 *
 * - The effective priority of the previous resource owner might be
 * lowered to its base priority value as a consequence of the priority
 * inheritance boost being cleared.
 *
 * - The synchronization object ownership is transfered to the
 * unblocked thread.
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
struct xnthread *xnsynch_release(struct xnsynch *synch)
{
	return xnsynch_release_thread(synch, xnpod_current_thread());
}
EXPORT_SYMBOL_GPL(xnsynch_release);

/*!
 * \fn struct xnthread *xnsynch_peek_pendq(struct xnsynch *synch);
 * \brief Access the thread leading a synch object wait queue.
 *
 * This services returns the descriptor address of to the thread leading a
 * synchronization object wait queue.
 *
 * @param synch The descriptor address of the target synchronization object.
 *
 * @return The descriptor address of the unblocked thread.
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
struct xnthread *xnsynch_peek_pendq(struct xnsynch *synch)
{
	struct xnthread *thread = NULL;
	struct xnpholder *holder;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	holder = getheadpq(&synch->pendq);
	if (holder)
		thread = link2thread(holder, plink);
	xnlock_put_irqrestore(&nklock, s);

	return thread;
}
EXPORT_SYMBOL_GPL(xnsynch_peek_pendq);

/*!
 * \fn void xnsynch_flush(struct xnsynch *synch, xnflags_t reason);
 * \brief Unblock all waiters pending on a resource.
 *
 * This service atomically releases all threads which currently sleep
 * on a given resource.
 *
 * This service should be called by upper interfaces under
 * circumstances requiring that the pending queue of a given resource
 * is cleared, such as before the resource is deleted.
 *
 * @param synch The descriptor address of the synchronization object
 * to be flushed.
 *
 * @param reason Some flags to set in the information mask of every
 * unblocked thread. Zero is an acceptable value. The following bits
 * are pre-defined by the nucleus:
 *
 * - XNRMID should be set to indicate that the synchronization object
 * is about to be destroyed (see xnpod_resume_thread()).
 *
 * - XNBREAK should be set to indicate that the wait has been forcibly
 * interrupted (see xnpod_unblock_thread()).
 *
 * @return XNSYNCH_RESCHED is returned if at least one thread
 * is unblocked, which means the caller should invoke xnpod_schedule()
 * for applying the new scheduling state. Otherwise, XNSYNCH_DONE is
 * returned.
 *
 * Side-effects:
 *
 * - The effective priority of the previous resource owner might be
 * lowered to its base priority value as a consequence of the priority
 * inheritance boost being cleared.
 *
 * - The synchronization object is no more owned by any thread.
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

int xnsynch_flush(struct xnsynch *synch, xnflags_t reason)
{
	struct xnpholder *holder;
	int status;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	trace_mark(xn_nucleus, synch_flush, "synch %p reason %lu",
		   synch, reason);

	status = emptypq_p(&synch->pendq) ? XNSYNCH_DONE : XNSYNCH_RESCHED;

	while ((holder = getpq(&synch->pendq)) != NULL) {
		struct xnthread *sleeper = link2thread(holder, plink);
		xnthread_set_info(sleeper, reason);
		sleeper->wchan = NULL;
		xnpod_resume_thread(sleeper, XNPEND);
	}

	if (testbits(synch->status, XNSYNCH_CLAIMED)) {
		xnsynch_clear_boost(synch, synch->owner);
		status = XNSYNCH_RESCHED;
	}

	xnlock_put_irqrestore(&nklock, s);

	xnarch_post_graph_if(synch, 0, emptypq_p(&synch->pendq));

	return status;
}
EXPORT_SYMBOL_GPL(xnsynch_flush);

/*!
 * @internal
 * \fn void xnsynch_forget_sleeper(struct xnthread *thread);
 * \brief Abort a wait for a resource.
 *
 * Performs all the necessary housekeeping chores to stop a thread
 * from waiting on a given synchronization object.
 *
 * @param thread The descriptor address of the affected thread.
 *
 * When the trace support is enabled (i.e. MVM), the idle state is
 * posted to the synchronization object's state diagram (if any)
 * whenever no thread remains blocked on it. The real-time interfaces
 * must ensure that such condition (i.e. EMPTY/IDLE) is mapped to
 * state #0.
 *
 * @note This routine must be entered nklock locked, interrupts off.
 */

void xnsynch_forget_sleeper(struct xnthread *thread)
{
	struct xnsynch *synch = thread->wchan;
	struct xnthread *owner, *target;
	struct xnpholder *h;

	trace_mark(xn_nucleus, synch_forget,
		   "thread %p thread_name %s synch %p",
		   thread, xnthread_name(thread), synch);

	xnthread_clear_state(thread, XNPEND);
	thread->wchan = NULL;
	removepq(&synch->pendq, &thread->plink);

	if (testbits(synch->status, XNSYNCH_CLAIMED)) {
		/* Find the highest priority needed to enforce the PIP. */
		owner = synch->owner;

		if (emptypq_p(&synch->pendq))
			/* No more sleepers: clear the boost. */
			xnsynch_clear_boost(synch, owner);
		else {
			target = link2thread(getheadpq(&synch->pendq), plink);
			h = getheadpq(&owner->claimq);
			if (w_cprio(target) != h->prio) {
				/*
				 * Reorder the claim queue, and lower
				 * the priority to the required
				 * minimum needed to prevent priority
				 * inversion.
				 */
				removepq(&owner->claimq, &synch->link);
				insertpqf(&owner->claimq, &synch->link,
					  w_cprio(target));

				h = getheadpq(&owner->claimq);
				if (h->prio < w_cprio(owner))
					xnsynch_renice_thread(owner, target);
			}
		}
	}

	xnarch_post_graph_if(synch, 0, emptypq_p(&synch->pendq));
}
EXPORT_SYMBOL_GPL(xnsynch_forget_sleeper);

/*!
 * @internal
 * \fn void xnsynch_release_all_ownerships(struct xnthread *thread);
 * \brief Release all ownerships.
 *
 * This call is used internally to release all the ownerships obtained
 * by a thread on synchronization objects. This routine must be
 * entered interrupts off.
 *
 * @param thread The descriptor address of the affected thread.
 *
 * @note This routine must be entered nklock locked, interrupts off.
 */

void xnsynch_release_all_ownerships(struct xnthread *thread)
{
	struct xnpholder *holder, *nholder;
	struct xnsynch *synch;

	for (holder = getheadpq(&thread->claimq); holder != NULL;
	     holder = nholder) {
		/*
		 * Since xnsynch_release() alters the claim queue, we
		 * need to be conservative while scanning it.
		 */
		synch = link2synch(holder);
		nholder = nextpq(&thread->claimq, holder);
		xnsynch_release_thread(synch, thread);
		if (synch->cleanup)
			synch->cleanup(synch);
	}
}
EXPORT_SYMBOL_GPL(xnsynch_release_all_ownerships);

#if XENO_DEBUG(SYNCH_RELAX)

/*
 * Detect when a thread is about to sleep on a synchronization
 * object currently owned by someone running in secondary mode.
 */
void xnsynch_detect_relaxed_owner(struct xnsynch *synch, struct xnthread *sleeper)
{
	if (xnthread_test_state(sleeper, XNTRAPSW) &&
	    !xnthread_test_info(sleeper, XNSWREP) &&
	    xnthread_test_state(synch->owner, XNRELAX)) {
		xnthread_set_info(sleeper, XNSWREP);
		xnshadow_send_sig(sleeper, SIGDEBUG,
				  SIGDEBUG_MIGRATE_PRIOINV, 1);
	} else
		xnthread_clear_info(sleeper,  XNSWREP);
}

/*
 * Detect when a thread is about to relax while holding a
 * synchronization object currently claimed by another thread, which
 * bears the TWARNSW bit (thus advertising a concern about potential
 * spurious relaxes and priority inversion). By relying on the claim
 * queue, we restrict the checks to PIP-enabled objects, but that
 * already covers most of the use cases anyway.
 */
void xnsynch_detect_claimed_relax(struct xnthread *owner)
{
	struct xnpholder *hs, *ht;
	struct xnthread *sleeper;
	struct xnsynch *synch;

	for (hs = getheadpq(&owner->claimq); hs != NULL;
	     hs = nextpq(&owner->claimq, hs)) {
		synch = link2synch(hs);
		for (ht = getheadpq(&synch->pendq); ht != NULL;
		     ht = nextpq(&synch->pendq, ht)) {
			sleeper = link2thread(ht, plink);
			if (xnthread_test_state(sleeper, XNTRAPSW)) {
				xnthread_set_info(sleeper, XNSWREP);
				xnshadow_send_sig(sleeper, SIGDEBUG,
						  SIGDEBUG_MIGRATE_PRIOINV, 1);
			}
		}
	}
}

#endif /* XENO_DEBUG(SYNCH_RELAX) */


/*@}*/

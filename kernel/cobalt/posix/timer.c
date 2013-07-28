/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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
 * @addtogroup cobalt_time
 *
 *@{*/

#include <linux/cred.h>
#include <linux/err.h>
#include "internal.h"
#include "thread.h"
#include "signal.h"
#include "timer.h"
#include "clock.h"

struct cobalt_timer *cobalt_timer_pool;

static struct list_head timer_freeq;

static void timer_handler(struct xntimer *xntimer)
{
	struct cobalt_timer *timer;

	timer = container_of(xntimer, struct cobalt_timer, timerbase);
	cobalt_signal_send_pid(timer->target, &timer->sigp);
}

static inline struct cobalt_thread *
timer_init(struct cobalt_timer *timer,
	   const struct sigevent *__restrict__ evp)
{
	struct cobalt_thread *owner = cobalt_current_thread(), *target;

	/*
	 * First, try to offload this operation to the extended
	 * personality the current thread might originate from.
	 */
	if (cobalt_initcall_extension(timer_init, &timer->extref,
				      owner, target, evp))
		return target;

	/*
	 * Ok, this did not work out, handle this the pure Cobalt way
	 * then. We currently supports SIGEV_THREAD_ID only when evp
	 * is specified.
	 */
	if (evp == NULL)
		return owner;

	if (evp->sigev_notify != SIGEV_THREAD_ID)
		return ERR_PTR(-EINVAL);

	/*
	 * Recipient thread must be a Xenomai shadow in user-space,
	 * living in the same process than our caller.
	 */
	target = cobalt_thread_find(evp->sigev_notify_thread_id);
	if (target == NULL || target->hkey.mm != current->mm)
		return ERR_PTR(-EINVAL);

	return target;
}

/**
 * Create a timer object.
 *
 * This service creates a time object using the clock @a clockid.
 *
 * If @a evp is not @a NULL, it describes the notification mechanism
 * used on timer expiration. Only thread-directed notification is
 * supported (evp->sigev_notify set to @a SIGEV_THREAD_ID).
 *
 * If @a evp is NULL, the current Cobalt thread will receive the
 * notifications with signal SIGALRM.
 *
 * The recipient thread is delivered notifications when it calls any
 * of the sigwait(), sigtimedwait() or sigwaitinfo() services.
 *
 * If this service succeeds, an identifier for the created timer is
 * returned at the address @a timerid. The timer is unarmed until
 * started with the timer_settime() service.
 *
 * @param clockid clock used as a timing base;
 *
 * @param evp description of the asynchronous notification to occur
 * when the timer expires;
 *
 * @param timerid address where the identifier of the created timer
 * will be stored on success.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, the clock @a clockid is invalid;
 * - EINVAL, the member @a sigev_notify of the @b sigevent structure at the
 *   address @a evp is not SIGEV_THREAD_ID;
 * - EINVAL, the  member @a sigev_signo of the @b sigevent structure is an
 *   invalid signal number;
 * - EAGAIN, the maximum number of timers was exceeded, recompile with a larger
 *   value.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/timer_create.html">
 * Specification.</a>
 *
 */
static inline int timer_create(clockid_t clockid,
			       const struct sigevent *__restrict__ evp,
			       timer_t * __restrict__ timerid)
{
	struct cobalt_thread *target;
	struct cobalt_timer *timer;
	int signo, ret = -EINVAL;
	timer_t timer_id;
	spl_t s;

	if (clockid != CLOCK_MONOTONIC &&
	    clockid != CLOCK_MONOTONIC_RAW &&
	    clockid != CLOCK_REALTIME)
		return -EINVAL;

	xnlock_get_irqsave(&nklock, s);

	if (list_empty(&timer_freeq)) {
		ret = -EAGAIN;
		goto unlock_and_error;
	}

	/* We may bail out early, don't unlink yet. */
	timer = list_first_entry(&timer_freeq, struct cobalt_timer, link);
	timer_id = (timer_t)(timer - cobalt_timer_pool);

	if (evp == NULL) {
		timer->sigp.si.si_int = timer_id;
		signo = SIGALRM;
	} else {
		signo = evp->sigev_signo;
		if (signo < 1 || signo > _NSIG)
			goto unlock_and_error;

		timer->sigp.si.si_value = evp->sigev_value;
	}

	timer->sigp.si.si_signo = signo;
	timer->sigp.si.si_errno = 0;
	timer->sigp.si.si_code = SI_TIMER;
	timer->sigp.si.si_tid = timer_id;
	timer->sigp.si.si_overrun = 0;
	INIT_LIST_HEAD(&timer->sigp.next);
	timer->clockid = clockid;
	timer->overruns = 0;

	target = timer_init(timer, evp);
	if (target == NULL)
		goto unlock_and_error;
	if (IS_ERR(target)) {
		ret = PTR_ERR(target);
		goto unlock_and_error;
	}

	xntimer_init(&timer->timerbase, &nkclock, timer_handler);
	timer->target = xnthread_host_pid(&target->threadbase);
	timer->owner = NULL;
	timer->owningq = cobalt_kqueues(0);
	list_del(&timer->link);
	list_add_tail(&timer->link, &cobalt_kqueues(0)->timerq);
	xnlock_put_irqrestore(&nklock, s);

	*timerid = timer_id;

	return 0;

unlock_and_error:
	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

static inline int
timer_delete(timer_t timerid, struct cobalt_kqueues *q, int force)
{
	struct cobalt_timer *timer;
	int ret = 0;
	spl_t s;

	if ((unsigned int)timerid >= CONFIG_XENO_OPT_NRTIMERS)
		return -EINVAL;

	xnlock_get_irqsave(&nklock, s);

	timer = &cobalt_timer_pool[(unsigned int)timerid];

	if (!xntimer_active_p(&timer->timerbase)) {
		ret = -EINVAL;
		goto out;
	}

	if (!force && timer->owningq != cobalt_kqueues(0)) {
		ret = -EPERM;
		goto out;
	}

	xntimer_destroy(&timer->timerbase);

	list_del(&timer->link);

	if (timer->owner)
		list_del(&timer->tlink);

	if (!list_empty(&timer->sigp.next))
		list_del(&timer->sigp.next);

	timer->owner = NULL;
	cobalt_call_extension(timer_cleanup, &timer->extref, ret);
	list_add(&timer->link, &timer_freeq); /* Favour earliest reuse. */
out:
	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

static inline void
timer_gettimeout(struct cobalt_timer *__restrict__ timer,
		 struct itimerspec *__restrict__ value)
{
	if (xntimer_running_p(&timer->timerbase)) {
		ns2ts(&value->it_value,
		      xntimer_get_timeout(&timer->timerbase));
		ns2ts(&value->it_interval,
		      xntimer_interval(&timer->timerbase));
	} else {
		value->it_value.tv_sec = 0;
		value->it_value.tv_nsec = 0;
		value->it_interval.tv_sec = 0;
		value->it_interval.tv_nsec = 0;
	}
}

/**
 * Start or stop a timer.
 *
 * This service sets a timer expiration date and reload value of the
 * timer @a timerid. If @a ovalue is not @a NULL, the current
 * expiration date and reload value are stored at the address @a
 * ovalue as with timer_gettime().
 *
 * If the member @a it_value of the @b itimerspec structure at @a
 * value is zero, the timer is stopped, otherwise the timer is
 * started. If the member @a it_interval is not zero, the timer is
 * periodic. The current thread must be a Cobalt thread (created with
 * pthread_create()) and will be notified via signal of timer
 * expirations. Note that these notifications will cause user-space
 * threads to switch to secondary mode.
 *
 * When starting the timer, if @a flags is TIMER_ABSTIME, the expiration value
 * is interpreted as an absolute date of the clock passed to the timer_create()
 * service. Otherwise, the expiration value is interpreted as a time interval.
 *
 * Expiration date and reload value are rounded to an integer count of
 * nanoseconds.
 *
 * @param timerid identifier of the timer to be started or stopped;
 *
 * @param flags one of 0 or TIMER_ABSTIME;
 *
 * @param value address where the specified timer expiration date and reload
 * value are read;
 *
 * @param ovalue address where the specified timer previous expiration date and
 * reload value are stored if not @a NULL.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EPERM, the caller context is invalid;
 * - EINVAL, the specified timer identifier, expiration date or reload value is
 *   invalid;
 * - EPERM, the timer @a timerid does not belong to the current process.
 *
 * @par Valid contexts:
 * - Cobalt kernel-space thread,
 * - kernel-space thread cancellation cleanup routine,
 * - Cobalt user-space thread (switches to primary mode),
 * - user-space thread cancellation cleanup routine.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/timer_settime.html">
 * Specification.</a>
 *
 */
static inline int
timer_settime(timer_t timerid, int flags,
	      const struct itimerspec *__restrict__ value,
	      struct itimerspec *__restrict__ ovalue)
{
	struct cobalt_thread *cur = cobalt_current_thread();
	xnticks_t start, period, now;
	struct cobalt_timer *timer;
	spl_t s;
	int ret;

	if (cur == NULL) {
		ret = -EPERM;
		goto error;
	}

	if ((unsigned int)timerid >= CONFIG_XENO_OPT_NRTIMERS) {
		ret = -EINVAL;
		goto error;
	}

	if ((unsigned long)value->it_value.tv_nsec >= ONE_BILLION ||
	    ((unsigned long)value->it_interval.tv_nsec >= ONE_BILLION &&
	     (value->it_value.tv_sec != 0 || value->it_value.tv_nsec != 0))) {
		ret = -EINVAL;
		goto error;
	}

	xnlock_get_irqsave(&nklock, s);

	timer = &cobalt_timer_pool[(unsigned int)timerid];

	if (!xntimer_active_p(&timer->timerbase)) {
		ret = -EINVAL;
		goto unlock_and_error;
	}

#if XENO_DEBUG(COBALT)
	if (timer->owningq != cobalt_kqueues(0)) {
		ret = -EPERM;
		goto unlock_and_error;
	}
#endif /* XENO_DEBUG(COBALT) */

	if (ovalue)
		timer_gettimeout(timer, ovalue);

	if (timer->owner)
		list_del(&timer->tlink);

	if (value->it_value.tv_nsec == 0 && value->it_value.tv_sec == 0) {
		xntimer_stop(&timer->timerbase);
		timer->owner = NULL;
	} else {
		start = ts2ns(&value->it_value) + 1;
		period = ts2ns(&value->it_interval);
		xntimer_set_sched(&timer->timerbase, xnpod_current_sched());
		/*
		 * Now start the timer. If the initial delay has
		 * already passed, the call shall succeed, so, let us
		 * tweak the start time until it fits.
		 */
		if (xntimer_start(&timer->timerbase, start, period,
				  clock_flag(flags, timer->clockid))) {
			now = clock_get_ticks(timer->clockid);
			if (period) {
				do
					start += period;
				while ((xnsticks_t) (start - now) <= 0);
			} else
				start = now +
					xnclock_ticks_to_ns(&nkclock,
							    nkclock.gravity);
			xntimer_start(&timer->timerbase, start, period,
				      clock_flag(flags, timer->clockid));
		}

		timer->owner = cur;
		list_add_tail(&timer->tlink, &timer->owner->timersq);
	}

	xnlock_put_irqrestore(&nklock, s);

	return 0;

unlock_and_error:
	xnlock_put_irqrestore(&nklock, s);
error:
	return ret;
}

/**
 * Get timer next expiration date and reload value.
 *
 * This service stores, at the address @a value, the expiration date
 * (member @a it_value) and reload value (member @a it_interval) of
 * the timer @a timerid. The values are returned as time intervals,
 * and as multiples of the system clock tick duration (see note in
 * section @ref cobalt_time "Clocks and timers services" for details
 * on the duration of the system clock tick). If the timer was not
 * started, the returned members @a it_value and @a it_interval of @a
 * value are zero.
 *
 * @param timerid timer identifier;
 *
 * @param value address where the timer expiration date and reload value are
 * stored on success.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, @a timerid is invalid;
 * - EPERM, the timer @a timerid does not belong to the current process.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/timer_gettime.html">
 * Specification.</a>
 *
 */
static inline int timer_gettime(timer_t timerid, struct itimerspec *value)
{
	struct cobalt_timer *timer;
	spl_t s;
	int ret;

	if ((unsigned int)timerid >= CONFIG_XENO_OPT_NRTIMERS) {
		ret = -EINVAL;
		goto error;
	}

	xnlock_get_irqsave(&nklock, s);

	timer = &cobalt_timer_pool[(unsigned int)timerid];

	if (!xntimer_active_p(&timer->timerbase)) {
		ret = -EINVAL;
		goto unlock_and_error;
	}

#if XENO_DEBUG(COBALT)
	if (timer->owningq != cobalt_kqueues(0)) {
		ret = -EPERM;
		goto unlock_and_error;
	}
#endif /* XENO_DEBUG(COBALT) */

	timer_gettimeout(timer, value);

	xnlock_put_irqrestore(&nklock, s);

	return 0;

unlock_and_error:
	xnlock_put_irqrestore(&nklock, s);
error:
	return ret;
}

int cobalt_timer_delete(timer_t timerid)
{
	return timer_delete(timerid, cobalt_kqueues(0), 0);
}

int cobalt_timer_create(clockid_t clock,
			const struct sigevent __user *u_sev,
			timer_t __user *u_tm)
{
	struct sigevent sev, *evp = NULL;
	timer_t tm = 0;
	int ret;

	if (u_sev) {
		evp = &sev;
		if (__xn_safe_copy_from_user(&sev, u_sev, sizeof(sev)))
			return -EFAULT;
	}

	ret = timer_create(clock, evp, &tm);
	if (ret)
		return ret;

	if (__xn_safe_copy_to_user(u_tm, &tm, sizeof(tm))) {
		cobalt_timer_delete(tm);
		return -EFAULT;
	}

	return 0;
}

int cobalt_timer_settime(timer_t tm, int flags,
			 const struct itimerspec __user *u_newval,
			 struct itimerspec __user *u_oldval)
{
	struct itimerspec newv, oldv, *oldvp;
	int ret;

	oldvp = u_oldval == 0 ? NULL : &oldv;

	if (__xn_safe_copy_from_user(&newv, u_newval, sizeof(newv)))
		return -EFAULT;

	ret = timer_settime(tm, flags, &newv, oldvp);
	if (ret)
		return ret;

	if (oldvp && __xn_safe_copy_to_user(u_oldval, oldvp, sizeof(oldv))) {
		timer_settime(tm, flags, oldvp, NULL);
		return -EFAULT;
	}

	return 0;
}

int cobalt_timer_gettime(timer_t tm, struct itimerspec __user *u_val)
{
	struct itimerspec val;
	int ret;

	ret = timer_gettime(tm, &val);
	if (ret)
		return ret;

	return __xn_safe_copy_to_user(u_val, &val, sizeof(val));
}

int cobalt_timer_getoverrun(timer_t timerid)
{
	struct cobalt_timer *timer;
	int overruns, ret;
	spl_t s;

	if ((unsigned int)timerid >= CONFIG_XENO_OPT_NRTIMERS) {
		ret = -EINVAL;
		goto error;
	}

	xnlock_get_irqsave(&nklock, s);

	timer = &cobalt_timer_pool[(unsigned int)timerid];

	if (!xntimer_active_p(&timer->timerbase)) {
		ret = -EINVAL;
		goto unlock_and_error;
	}

#if XENO_DEBUG(COBALT)
	if (timer->owningq != cobalt_kqueues(0)) {
		ret = -EPERM;
		goto unlock_and_error;
	}
#endif /* XENO_DEBUG(COBALT) */

	overruns = timer->overruns;

	xnlock_put_irqrestore(&nklock, s);

	return overruns;

unlock_and_error:
	xnlock_put_irqrestore(&nklock, s);
error:
	return ret;
}

int cobalt_timer_deliver(timer_t timerid) /* nklocked, IRQs off. */
{
	struct cobalt_timer *timer;
	xnticks_t now;

	timer = &cobalt_timer_pool[(unsigned int)timerid];

	if (!xntimer_interval(&timer->timerbase))
		timer->overruns = 0;
	else {
		now = xnclock_read_raw(&nkclock);
		timer->overruns = xntimer_get_overruns(&timer->timerbase, now);
		if ((unsigned int)timer->overruns > COBALT_DELAYMAX)
			timer->overruns = COBALT_DELAYMAX;
	}

	return timer->overruns;
}

void cobalt_timer_flush(struct cobalt_thread *thread) /* nklocked, IRQs off. */
{
	struct cobalt_timer *timer, *tmp;

	if (list_empty(&thread->timersq))
		return;

	list_for_each_entry_safe(timer, tmp, &thread->timersq, tlink) {
		list_del(&timer->tlink);
		xntimer_stop(&timer->timerbase);
		timer->owner = NULL;
	}
}

void cobalt_timerq_cleanup(struct cobalt_kqueues *q)
{
	struct cobalt_timer *timer, *tmp;
	timer_t tm;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (list_empty(&q->timerq))
		goto out;

	list_for_each_entry_safe(timer, tmp, &q->timerq, link) { 
		tm = (timer_t)(timer - cobalt_timer_pool);
		timer_delete(tm, q, 1);
		xnlock_put_irqrestore(&nklock, s);
#if XENO_DEBUG(COBALT)
		printk(XENO_INFO "deleting Cobalt timer %u\n", (unsigned int)tm);
#endif /* XENO_DEBUG(COBALT) */
		xnlock_get_irqsave(&nklock, s);
	}
out:
	xnlock_put_irqrestore(&nklock, s);
}

#define __TMPOOL_SIZE  (sizeof(struct cobalt_timer) * CONFIG_XENO_OPT_NRTIMERS)

int cobalt_timer_pkg_init(void)
{
	int n;

	cobalt_timer_pool = alloc_pages_exact(__TMPOOL_SIZE, GFP_KERNEL);
	if (cobalt_timer_pool == NULL)
		return -ENOMEM;

	INIT_LIST_HEAD(&timer_freeq);
	INIT_LIST_HEAD(&cobalt_global_kqueues.timerq);

	for (n = 0; n < CONFIG_XENO_OPT_NRTIMERS; n++)
		list_add_tail(&cobalt_timer_pool[n].link, &timer_freeq);

	return 0;
}

void cobalt_timer_pkg_cleanup(void)
{
	cobalt_timerq_cleanup(&cobalt_global_kqueues);
	free_pages_exact(cobalt_timer_pool, __TMPOOL_SIZE);
}

/*@}*/

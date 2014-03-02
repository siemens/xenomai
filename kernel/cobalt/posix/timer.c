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

#include <linux/module.h>
#include <linux/cred.h>
#include <linux/err.h>
#include "internal.h"
#include "thread.h"
#include "signal.h"
#include "timer.h"
#include "clock.h"

void cobalt_timer_handler(struct xntimer *xntimer)
{
	struct cobalt_timer *timer;
	/*
	 * Deliver the timer notification via a signal (unless
	 * SIGEV_NONE was given). If we can't do this because the
	 * target thread disappeared, then stop the timer. It will go
	 * away when timer_delete() is called, or the owner's process
	 * exits, whichever comes first.
	 */
	timer = container_of(xntimer, struct cobalt_timer, timerbase);
	if (timer->sigp.si.si_signo &&
	    cobalt_signal_send_pid(timer->target, &timer->sigp) == -ESRCH)
		xntimer_stop(&timer->timerbase);
}
EXPORT_SYMBOL_GPL(cobalt_timer_handler);

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
				      owner, target, evp) && target)
		return target;

	/*
	 * Ok, we have no extension available, or we do but it does
	 * not want to overload the standard behavior: handle this
	 * timer the pure Cobalt way then. We only know about standard
	 * clocks in this case.
	 */
	if (timer->clockid != CLOCK_MONOTONIC &&
	    timer->clockid != CLOCK_MONOTONIC_RAW &&
	    timer->clockid != CLOCK_REALTIME)
		return ERR_PTR(-EINVAL);

	if (evp == NULL || evp->sigev_notify == SIGEV_NONE)
		return owner;	/* Assume SIGEV_THREAD_ID. */

	if (evp->sigev_notify != SIGEV_THREAD_ID)
		return ERR_PTR(-EINVAL);

	/*
	 * Recipient thread must be a Xenomai shadow in user-space,
	 * living in the same process than our caller.
	 */
	target = cobalt_thread_find_local(evp->sigev_notify_thread_id);
	if (target == NULL)
		return ERR_PTR(-EINVAL);

	/*
	 * All standard clocks are based on the core clock, and we
	 * want to deliver a signal when a timer elapses.
	 */
	xntimer_init(&timer->timerbase, &nkclock, cobalt_timer_handler,
		     &target->threadbase);

	return target;
}

static inline int timer_alloc_id(struct cobalt_process *cc)
{
	int id;

	id = find_first_bit(cc->timers_map, CONFIG_XENO_OPT_NRTIMERS);
	if (id == CONFIG_XENO_OPT_NRTIMERS)
		return -EAGAIN;

	__clear_bit(id, cc->timers_map);

	return id;
}

static inline void timer_free_id(struct cobalt_process *cc, int id)
{
	__set_bit(id, cc->timers_map);
}

struct cobalt_timer *
cobalt_timer_by_id(struct cobalt_process *cc, timer_t timer_id)
{
	if (timer_id < 0 || timer_id >= CONFIG_XENO_OPT_NRTIMERS)
		return NULL;

	if (test_bit(timer_id, cc->timers_map))
		return NULL;
	
	return cc->timers[timer_id];
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
	struct cobalt_process *cc;
	struct cobalt_thread *target;
	struct cobalt_timer *timer;
	int signo, ret = -EINVAL;
	timer_t timer_id;
	spl_t s;

	cc = cobalt_process_context();
	if (cc == NULL)
		return -EPERM;

	timer = kmalloc(sizeof(*timer), GFP_KERNEL);
	if (timer == NULL)
		return -ENOMEM;
	
	timer->sigp.si.si_errno = 0;
	timer->sigp.si.si_code = SI_TIMER;
	timer->sigp.si.si_overrun = 0;
	INIT_LIST_HEAD(&timer->sigp.next);
	timer->clockid = clockid;
	timer->overruns = 0;

	xnlock_get_irqsave(&nklock, s);

	ret = timer_alloc_id(cc);
	if (ret < 0)
		goto out;

	timer_id = ret;

	if (evp == NULL) {
		timer->sigp.si.si_int = timer_id;
		signo = SIGALRM;
	} else {
		if (evp->sigev_notify == SIGEV_NONE)
			signo = 0; /* Don't notify. */
		else {
			signo = evp->sigev_signo;
			if (signo < 1 || signo > _NSIG)
				goto fail;
			timer->sigp.si.si_value = evp->sigev_value;
		}
	}

	timer->sigp.si.si_signo = signo;
	timer->sigp.si.si_tid = timer_id;
	timer->id = timer_id;

	target = timer_init(timer, evp);
	if (target == NULL)
		goto fail;

	if (IS_ERR(target)) {
		ret = PTR_ERR(target);
		goto fail;
	}

	timer->target = xnthread_host_pid(&target->threadbase);
	cc->timers[timer_id] = timer;

	xnlock_put_irqrestore(&nklock, s);

	*timerid = timer_id;

	return 0;
fail:
	timer_free_id(cc, timer_id);
out:
	xnlock_put_irqrestore(&nklock, s);

	kfree(timer);

	return ret;
}

static void timer_cleanup(struct cobalt_process *p, struct cobalt_timer *timer)
{
	xntimer_destroy(&timer->timerbase);

	if (!list_empty(&timer->sigp.next))
		list_del(&timer->sigp.next);

	timer_free_id(p, cobalt_timer_id(timer));
	p->timers[cobalt_timer_id(timer)] = NULL;
}

static inline int
timer_delete(timer_t timerid)
{
	struct cobalt_process *cc;
	struct cobalt_timer *timer;
	int ret = 0;
	spl_t s;

	cc = cobalt_process_context();
	if (cc == NULL)
		return -EPERM;

	xnlock_get_irqsave(&nklock, s);

	timer = cobalt_timer_by_id(cc, timerid);
	if (timer == NULL) {
		xnlock_put_irqrestore(&nklock, s);
		return -EINVAL;
	}
	/*
	 * If an extension runs and actually handles the deletion, we
	 * should not call the timer_cleanup extension handler for
	 * this timer, but we shall destroy the core timer. If the
	 * handler returns on error, the whole deletion process is
	 * aborted, leaving the timer untouched. In all other cases,
	 * we do the core timer cleanup work, firing the timer_cleanup
	 * extension handler if defined.
	 */
  	if (cobalt_call_extension(timer_delete, &timer->extref, ret) && ret < 0)
		goto out;

	if (ret == 0)
		cobalt_call_extension(timer_cleanup, &timer->extref, ret);
	else
		ret = 0;

	timer_cleanup(cc, timer);
	xnlock_put_irqrestore(&nklock, s);
	kfree(timer);

	return ret;

out:
	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

static inline void
timer_gettimeout(struct cobalt_timer *__restrict__ timer,
		 struct itimerspec *__restrict__ value)
{
	int ret;

	if (!xntimer_running_p(&timer->timerbase)) {
		value->it_value.tv_sec = 0;
		value->it_value.tv_nsec = 0;
		value->it_interval.tv_sec = 0;
		value->it_interval.tv_nsec = 0;
		return;
	}

	if (!cobalt_call_extension(timer_gettime, &timer->extref,
				   ret, value) || ret == 0) {
		ns2ts(&value->it_value,
		      xntimer_get_timeout(&timer->timerbase));
		ns2ts(&value->it_interval,
		      xntimer_interval(&timer->timerbase));
	}
}

static inline int timer_set(struct cobalt_timer *timer, int flags,
			    const struct itimerspec *__restrict__ value)
{				/* nklocked, IRQs off. */
	struct cobalt_thread *thread;
	xnticks_t start, period;
	int ret;

	/* First, try offloading the work to an extension. */

	if (cobalt_call_extension(timer_settime, &timer->extref,
				  ret, value, flags) && ret != 0)
		return ret < 0 ? ret : 0;

	/*
	 * No extension, or operation not handled. Default to plain
	 * POSIX behavior.
	 */

	if (value->it_value.tv_nsec == 0 && value->it_value.tv_sec == 0) {
		xntimer_stop(&timer->timerbase);
		return 0;
	}

	if ((unsigned long)value->it_value.tv_nsec >= ONE_BILLION ||
	    ((unsigned long)value->it_interval.tv_nsec >= ONE_BILLION &&
	     (value->it_value.tv_sec != 0 || value->it_value.tv_nsec != 0)))
		return -EINVAL;

	start = ts2ns(&value->it_value) + 1;
	period = ts2ns(&value->it_interval);

	/*
	 * If the target thread vanished, simply don't start the
	 * timer.
	 */
	thread = cobalt_thread_find(timer->target);
	if (thread == NULL)
		return 0;

	/*
	 * Make the timer affine to the CPU running the thread to be
	 * signaled.
	 */
	xntimer_set_sched(&timer->timerbase, thread->threadbase.sched);
	/*
	 * Now start the timer. If the timeout data has already
	 * passed, the caller will handle the case.
	 */
	return xntimer_start(&timer->timerbase, start, period,
			     clock_flag(flags, timer->clockid));
}

static inline void
timer_deliver_late(struct cobalt_process *cc, timer_t timerid)
{
	struct cobalt_timer *timer;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	/*
	 * We dropped the lock shortly, revalidate the timer handle in
	 * case a deletion slipped in.
	 */
	timer = cobalt_timer_by_id(cc, timerid);
	if (timer)
		cobalt_timer_handler(&timer->timerbase);

	xnlock_put_irqrestore(&nklock, s);
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
 * - EINVAL, the specified timer identifier, expiration date or reload value is
 *   invalid. For @a timerid to be valid, it must belong to the current process.
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
	struct cobalt_timer *timer;
	struct cobalt_process *cc;
	int ret;
	spl_t s;

	cc = cobalt_process_context();
	XENO_BUGON(COBALT, cc == NULL);

	xnlock_get_irqsave(&nklock, s);

	timer = cobalt_timer_by_id(cc, timerid);
	if (timer == NULL) {
		ret = -EINVAL;
		goto out;
	}

	if (ovalue)
		timer_gettimeout(timer, ovalue);

	ret = timer_set(timer, flags, value);
	if (ret == -ETIMEDOUT) {
		/*
		 * Time has already passed, deliver a notification
		 * immediately. Since we are about to dive into the
		 * signal machinery for this, let's drop the nklock to
		 * break the atomic section temporarily.
		 */
		xnlock_put_irqrestore(&nklock, s);
		timer_deliver_late(cc, timerid);
		return 0;
	}
out:
	xnlock_put_irqrestore(&nklock, s);

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
 * - EINVAL, @a timerid is invalid. For @a timerid to be valid, it
 * must belong to the current process.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/timer_gettime.html">
 * Specification.</a>
 *
 */
static inline int timer_gettime(timer_t timerid, struct itimerspec *value)
{
	struct cobalt_timer *timer;
	struct cobalt_process *cc;
	spl_t s;

	cc = cobalt_process_context();
	if (cc == NULL)
		return -EPERM;

	xnlock_get_irqsave(&nklock, s);

	timer = cobalt_timer_by_id(cc, timerid);
	if (timer == NULL)
		goto fail;

	timer_gettimeout(timer, value);

	xnlock_put_irqrestore(&nklock, s);

	return 0;
fail:
	xnlock_put_irqrestore(&nklock, s);

	return -EINVAL;
}

int cobalt_timer_delete(timer_t timerid)
{
	return timer_delete(timerid);
}

int cobalt_timer_create(clockid_t clock,
			const struct sigevent __user *u_sev,
			timer_t __user *u_tm)
{
	struct sigevent sev, *evp = NULL;
	timer_t timerid = 0;
	int ret;

	if (u_sev) {
		evp = &sev;
		if (__xn_safe_copy_from_user(&sev, u_sev, sizeof(sev)))
			return -EFAULT;
	}

	ret = timer_create(clock, evp, &timerid);
	if (ret)
		return ret;

	if (__xn_safe_copy_to_user(u_tm, &timerid, sizeof(timerid))) {
		timer_delete(timerid);
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
	struct cobalt_process *cc;
	int overruns;
	spl_t s;

	cc = cobalt_process_context();
	if (cc == NULL)
		return -EPERM;

	xnlock_get_irqsave(&nklock, s);

	timer = cobalt_timer_by_id(cc, timerid);
	if (timer == NULL)
		goto fail;

	overruns = timer->overruns;

	xnlock_put_irqrestore(&nklock, s);

	return overruns;
fail:
	xnlock_put_irqrestore(&nklock, s);

	return -EINVAL;
}

int cobalt_timer_deliver(timer_t timerid) /* nklocked, IRQs off. */
{
	struct cobalt_timer *timer;
	xnticks_t now;

	timer = cobalt_timer_by_id(cobalt_process_context(), timerid);
	if (timer == NULL)
		/* Killed before ultimate delivery, who cares then? */
		return 0;

	if (!xntimer_interval(&timer->timerbase))
		timer->overruns = 0;
	else {
		now = xnclock_read_raw(xntimer_clock(&timer->timerbase));
		timer->overruns = xntimer_get_overruns(&timer->timerbase, now);
		if ((unsigned int)timer->overruns > COBALT_DELAYMAX)
			timer->overruns = COBALT_DELAYMAX;
	}

	return timer->overruns;
}

void cobalt_timers_cleanup(struct cobalt_process *p)
{
	struct cobalt_timer *timer;
	unsigned id;
	spl_t s;
	int ret;

	xnlock_get_irqsave(&nklock, s);

	if (find_first_zero_bit(p->timers_map, CONFIG_XENO_OPT_NRTIMERS) ==
		CONFIG_XENO_OPT_NRTIMERS)
		goto out;

	for (id = 0; id < ARRAY_SIZE(p->timers); id++) {
		timer = cobalt_timer_by_id(p, id);
		if (timer == NULL)
			continue;

		cobalt_call_extension(timer_cleanup, &timer->extref, ret);
		timer_cleanup(p, timer);
		xnlock_put_irqrestore(&nklock, s);
		kfree(timer);
#if XENO_DEBUG(COBALT)
		printk(XENO_INFO "deleting Cobalt timer %u\n", id);
#endif /* XENO_DEBUG(COBALT) */
		xnlock_get_irqsave(&nklock, s);
	}
out:
	xnlock_put_irqrestore(&nklock, s);
}
/*@}*/

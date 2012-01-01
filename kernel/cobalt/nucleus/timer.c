/**
 * @file
 * @note Copyright (C) 2001,2002,2003,2007 Philippe Gerum <rpm@xenomai.org>.
 *       Copyright (C) 2004 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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
 * \ingroup timer
 */

/*!
 * \ingroup nucleus
 * \defgroup timer Timer services.
 *
 * The Xenomai timer facility always operate the timer hardware in
 * oneshot mode, regardless of the time base in effect. Periodic
 * timing is obtained through a software emulation, using cascading
 * timers.
 *
 * The timer object stores time as a count of CPU ticks (e.g. TSC
 * values).
 *
 *@{*/

#include <nucleus/pod.h>
#include <nucleus/thread.h>
#include <nucleus/timer.h>
#include <asm/xenomai/bits/timer.h>

static inline void xntimer_enqueue(xntimer_t *timer)
{
	xntimerq_t *q = &timer->sched->timerqueue;
	xntimerq_insert(q, &timer->aplink);
	__clrbits(timer->status, XNTIMER_DEQUEUED);
	xnstat_counter_inc(&timer->scheduled);
}

static inline void xntimer_dequeue(xntimer_t *timer)
{
	xntimerq_remove(&timer->sched->timerqueue, &timer->aplink);
	__setbits(timer->status, XNTIMER_DEQUEUED);
}

void xntimer_next_local_shot(xnsched_t *sched)
{
	struct xntimer *timer;
	xnsticks_t delay;
	xntimerq_it_t it;
	xntimerh_t *h;

	/*
	 * Do not reprogram locally when inside the tick handler -
	 * will be done on exit anyway. Also exit if there is no
	 * pending timer.
	 */
	if (testbits(sched->status, XNINTCK))
		return;

	h = xntimerq_it_begin(&sched->timerqueue, &it);
	if (h == NULL)
		return;

	/*
	 * Here we try to defer the host tick heading the timer queue,
	 * so that it does not preempt a real-time activity uselessly,
	 * in two cases:
	 *
	 * 1) a rescheduling is pending for the current CPU. We may
	 * assume that a real-time thread is about to resume, so we
	 * want to move the host tick out of the way until the host
	 * kernel resumes, unless there is no other outstanding
	 * timers.
	 *
	 * 2) the current thread is running in primary mode, in which
	 * case we may also defer the host tick until the host kernel
	 * resumes.
	 *
	 * The host tick deferral is cleared whenever Xenomai is about
	 * to yield control to the host kernel (see
	 * __xnpod_schedule()), or a timer with an earlier timeout
	 * date is scheduled, whichever comes first.
	 */
	__clrbits(sched->lflags, XNHDEFER);
	timer = aplink2timer(h);
	if (unlikely(timer == &sched->htimer)) {
		if (xnsched_resched_p(sched) ||
		    !xnthread_test_state(sched->curr, XNROOT)) {
			h = xntimerq_it_next(&sched->timerqueue, &it, h);
			if (h) {
				__setbits(sched->lflags, XNHDEFER);
				timer = aplink2timer(h);
			}
		}
	}

	delay = xntimerh_date(&timer->aplink) -
		(xnarch_get_cpu_tsc() + nklatency);

	if (delay < 0)
		delay = 0;
	else if (delay > ULONG_MAX)
		delay = ULONG_MAX;

	xnarch_trace_tick((unsigned)delay);

	xnarch_program_timer_shot(delay);
}

static inline int xntimer_heading_p(struct xntimer *timer)
{
	struct xnsched *sched = timer->sched;
	xntimerq_it_t it;
	xntimerh_t *h;

	h = xntimerq_it_begin(&sched->timerqueue, &it);
	if (h == &timer->aplink)
		return 1;

	if (testbits(sched->lflags, XNHDEFER)) {
		h = xntimerq_it_next(&sched->timerqueue, &it, h);
		if (h == &timer->aplink)
			return 1;
	}

	return 0;
}

static inline void xntimer_next_remote_shot(xnsched_t *sched)
{
	xnarch_send_timer_ipi(xnarch_cpumask_of_cpu(xnsched_cpu(sched)));
}

static void xntimer_adjust(xntimer_t *timer, xnsticks_t delta)
{
	xnticks_t period, mod;
	xnsticks_t diff;

	xntimerh_date(&timer->aplink) -= delta;

	if (!testbits(timer->status, XNTIMER_PERIODIC))
		goto enqueue;

	period = xntimer_interval(timer);
	timer->pexpect -= delta;
	diff = xnarch_get_cpu_tsc() - xntimerh_date(&timer->aplink);

	if ((xnsticks_t) (diff - period) >= 0) {
		/*
		 * Timer should tick several times before now, instead
		 * of calling timer->handler several times, we change
		 * the timer date without changing its pexpect, so
		 * that timer will tick only once and the lost ticks
		 * will be counted as overruns.
		 */
		mod = xnarch_mod64(diff, period);
		xntimerh_date(&timer->aplink) += diff - mod;
	} else if (delta < 0
		   && testbits(timer->status, XNTIMER_FIRED)
		   && (xnsticks_t) (diff + period) <= 0) {
		/*
		 * Timer is periodic and NOT waiting for its first
		 * shot, so we make it tick sooner than its original
		 * date in order to avoid the case where by adjusting
		 * time to a sooner date, real-time periodic timers do
		 * not tick until the original date has passed.
		 */
		mod = xnarch_mod64(-diff, period);
		xntimerh_date(&timer->aplink) += diff + mod;
		timer->pexpect += diff + mod;
	}

enqueue:
	xntimer_enqueue(timer);
}

void xntimer_adjust_all(xnsticks_t delta)
{
	unsigned cpu, nr_cpus;
	xnqueue_t adjq;

	initq(&adjq);
	delta = xnarch_ns_to_tsc(delta);
	for (cpu = 0, nr_cpus = xnarch_num_online_cpus(); cpu < nr_cpus; cpu++) {
		xnsched_t *sched = xnpod_sched_slot(cpu);
		xntimerq_t *q = &sched->timerqueue;
		xnholder_t *adjholder;
		xntimerh_t *holder;
		xntimerq_it_t it;

		for (holder = xntimerq_it_begin(q, &it); holder;
		     holder = xntimerq_it_next(q, &it, holder)) {
			xntimer_t *timer = aplink2timer(holder);
			if (testbits(timer->status, XNTIMER_REALTIME)) {
				inith(&timer->adjlink);
				appendq(&adjq, &timer->adjlink);
			}
		}

		while ((adjholder = getq(&adjq))) {
			xntimer_t *timer = adjlink2timer(adjholder);
			xntimer_dequeue(timer);
			xntimer_adjust(timer, delta);
		}

		if (sched != xnpod_current_sched())
			xntimer_next_remote_shot(sched);
		else
			xntimer_next_local_shot(sched);
	}
}

/*!
 * @fn void xntimer_start(xntimer_t *timer,xnticks_t value,xnticks_t interval,
 *                        xntmode_t mode)
 * @brief Arm a timer.
 *
 * Activates a timer so that the associated timeout handler will be
 * fired after each expiration time. A timer can be either periodic or
 * one-shot, depending on the reload value passed to this routine. The
 * given timer must have been previously initialized.
 *
 * @param timer The address of a valid timer descriptor.
 *
 * @param value The date of the initial timer shot, expressed in
 * nanoseconds.
 *
 * @param interval The reload value of the timer. It is a periodic
 * interval value to be used for reprogramming the next timer shot,
 * expressed in nanoseconds. If @a interval is equal to XN_INFINITE,
 * the timer will not be reloaded after it has expired.
 *
 * @param mode The timer mode. It can be XN_RELATIVE if @a value shall
 * be interpreted as a relative date, XN_ABSOLUTE for an absolute date
 * based on the monotonic clock of the related time base (as returned
 * my xnclock_read_monotonic()), or XN_REALTIME if the absolute date
 * is based on the adjustable real-time clock (obtained from
 * xnclock_read()).
 *
 * @return 0 is returned upon success, or -ETIMEDOUT if an absolute
 * date in the past has been given.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Any kernel context.
 *
 * Rescheduling: never.
 *
 * @note Must be called with nklock held, IRQs off.
 */
int xntimer_start(xntimer_t *timer,
		  xnticks_t value, xnticks_t interval,
		  xntmode_t mode)
{
	xnticks_t date, now;

	trace_mark(xn_nucleus, timer_start,
		   "timer %p value %Lu interval %Lu mode %u",
		   timer, value, interval, mode);

	if (!testbits(timer->status, XNTIMER_DEQUEUED))
		xntimer_dequeue(timer);

	now = xnarch_get_cpu_tsc();

	__clrbits(timer->status,
		  XNTIMER_REALTIME | XNTIMER_FIRED | XNTIMER_PERIODIC);
	switch (mode) {
	case XN_RELATIVE:
		if ((xnsticks_t)value < 0)
			return -ETIMEDOUT;
		date = xnarch_ns_to_tsc(value) + now;
		break;
	case XN_REALTIME:
		__setbits(timer->status, XNTIMER_REALTIME);
		value -= xnclock_get_offset();
		/* fall through */
	default: /* XN_ABSOLUTE || XN_REALTIME */
		date = xnarch_ns_to_tsc(value);
		if ((xnsticks_t)(date - now) <= 0)
			return -ETIMEDOUT;
		break;
	}

	xntimerh_date(&timer->aplink) = date;

	timer->interval = XN_INFINITE;
	if (interval != XN_INFINITE) {
		timer->interval = xnarch_ns_to_tsc(interval);
		timer->pexpect = date;
		__setbits(timer->status, XNTIMER_PERIODIC);
	}

	xntimer_enqueue(timer);
	if (xntimer_heading_p(timer)) {
		if (xntimer_sched(timer) != xnpod_current_sched())
			xntimer_next_remote_shot(xntimer_sched(timer));
		else
			xntimer_next_local_shot(xntimer_sched(timer));
	}

	return 0;
}
EXPORT_SYMBOL_GPL(xntimer_start);

/*!
 * \fn int xntimer_stop(xntimer_t *timer)
 *
 * \brief Disarm a timer.
 *
 * This service deactivates a timer previously armed using
 * xntimer_start(). Once disarmed, the timer can be subsequently
 * re-armed using the latter service.
 *
 * @param timer The address of a valid timer descriptor.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Any kernel context.
 *
 * Rescheduling: never.
 *
 * @note Must be called with nklock held, IRQs off.
 */
void __xntimer_stop(xntimer_t *timer)
{
	int heading;

	trace_mark(xn_nucleus, timer_stop, "timer %p", timer);

	heading = xntimer_heading_p(timer);
	xntimer_dequeue(timer);

	/* If we removed the heading timer, reprogram the next shot if
	   any. If the timer was running on another CPU, let it tick. */
	if (heading && xntimer_sched(timer) == xnpod_current_sched())
		xntimer_next_local_shot(xntimer_sched(timer));
}
EXPORT_SYMBOL_GPL(__xntimer_stop);

/*!
 * \fn xnticks_t xntimer_get_date(xntimer_t *timer)
 *
 * \brief Return the absolute expiration date.
 *
 * Return the next expiration date of a timer as an absolute count of
 * nanoseconds.
 *
 * @param timer The address of a valid timer descriptor.
 *
 * @return The expiration date in nanoseconds. The special value
 * XN_INFINITE is returned if @a timer is currently disabled.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Any kernel context.
 *
 * Rescheduling: never.
 */
xnticks_t xntimer_get_date(xntimer_t *timer)
{
	if (!xntimer_running_p(timer))
		return XN_INFINITE;

	return xnarch_tsc_to_ns(xntimerh_date(&timer->aplink));
}
EXPORT_SYMBOL_GPL(xntimer_get_date);

/*!
 * \fn xnticks_t xntimer_get_timeout(xntimer_t *timer)
 *
 * \brief Return the relative expiration date.
 *
 * This call returns the count of nanoseconds remaining until the
 * timer expires.
 *
 * @param timer The address of a valid timer descriptor.
 *
 * @return The count of nanoseconds until expiry. The special value
 * XN_INFINITE is returned if @a timer is currently disabled.  It
 * might happen that the timer expires when this service runs (even if
 * the associated handler has not been fired yet); in such a case, 1
 * is returned.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Any kernel context.
 *
 * Rescheduling: never.
 */
xnticks_t xntimer_get_timeout(xntimer_t *timer)
{
	xnticks_t tsc;

	if (!xntimer_running_p(timer))
		return XN_INFINITE;

	tsc = xnarch_get_cpu_tsc();
	if (xntimerh_date(&timer->aplink) < tsc)
		return 1;	/* Will elapse shortly. */

	return xnarch_tsc_to_ns(xntimerh_date(&timer->aplink) - tsc);
}
EXPORT_SYMBOL_GPL(xntimer_get_timeout);

/*!
 * \fn xnticks_t xntimer_get_interval(xntimer_t *timer)
 *
 * \brief Return the timer interval value.
 *
 * Return the timer interval value in nanoseconds.
 *
 * @param timer The address of a valid timer descriptor.
 *
 * @return The duration of a period in nanoseconds. The special value
 * XN_INFINITE is returned if @a timer is currently disabled or
 * one shot.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Any kernel context.
 *
 * Rescheduling: never.
 */
xnticks_t xntimer_get_interval(xntimer_t *timer)
{
	return xnarch_tsc_to_ns_rounded(timer->interval);
}
EXPORT_SYMBOL_GPL(xntimer_get_interval);

/*!
 * @internal
 * \fn void xntimer_tick(void)
 *
 * \brief Process a timer tick.
 *
 * This routine informs all active timers that the clock has been
 * updated by processing the outstanding timer list. Elapsed timer
 * actions will be fired.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Interrupt service routine, nklock locked, interrupts off
 *
 * Rescheduling: never.
 */

void xntimer_tick(void)
{
	xnsched_t *sched = xnpod_current_sched();
	xntimerq_t *timerq = &sched->timerqueue;
	xnticks_t now, interval;
	xntimerh_t *holder;
	xntimer_t *timer;
	xnsticks_t delta;

	/*
	 * Optimisation: any local timer reprogramming triggered by
	 * invoked timer handlers can wait until we leave the tick
	 * handler. Use this status flag as hint to xntimer_start().
	 */
	__setbits(sched->status, XNINTCK);

	now = xnarch_get_cpu_tsc();
	while ((holder = xntimerq_head(timerq)) != NULL) {
		timer = aplink2timer(holder);
		/*
		 * If the delay to the next shot is greater than the
		 * intrinsic latency value, we may stop scanning the
		 * timer queue there, since timeout dates are ordered
		 * by increasing values.
		 */
		delta = (xnsticks_t)(xntimerh_date(&timer->aplink) - now);
		if (delta > (xnsticks_t)(nklatency + nktimerlat))
			break;

		trace_mark(xn_nucleus, timer_expire, "timer %p", timer);

		xntimer_dequeue(timer);
		xnstat_counter_inc(&timer->fired);

		if (likely(timer != &sched->htimer)) {
			if (likely(!testbits(nkclock.status, XNTBLCK)
				   || testbits(timer->status, XNTIMER_NOBLCK))) {
				timer->handler(timer);
				now = xnarch_get_cpu_tsc();
				/*
				 * If the elapsed timer has no reload
				 * value, or was re-enqueued or killed
				 * by the timeout handler: don't not
				 * re-enqueue it for the next shot.
				 */
				if (!xntimer_reload_p(timer))
					continue;
				__setbits(timer->status, XNTIMER_FIRED);
			} else if (likely(!testbits(timer->status, XNTIMER_PERIODIC))) {
				/*
				 * Make the blocked timer elapse again
				 * at a reasonably close date in the
				 * future, waiting for the clock to be
				 * unlocked at some point. Timers are
				 * blocked when single-stepping into
				 * an application using a debugger, so
				 * it is fine to wait for 250 ms for
				 * the user to continue program
				 * execution.
				 */
				interval = xnarch_ns_to_tsc(250000000ULL);
				goto requeue;
			}
		} else {
			/*
			 * By postponing the propagation of the
			 * low-priority host tick to the interrupt
			 * epilogue (see xnintr_irq_handler()), we
			 * save some I-cache, which translates into
			 * precious microsecs on low-end hw.
			 */
			__setbits(sched->lflags, XNHTICK);
			__clrbits(sched->lflags, XNHDEFER);
			if (!testbits(timer->status, XNTIMER_PERIODIC))
				continue;
		}

		interval = timer->interval;
	requeue:
		do {
			xntimerh_date(&timer->aplink) += interval;
		} while (xntimerh_date(&timer->aplink) < now + nklatency);
		xntimer_enqueue(timer);
	}

	__clrbits(sched->status, XNINTCK);

	xntimer_next_local_shot(sched);
}

/*!
 * \fn void xntimer_init(xntimer_t *timer,void (*handler)(xntimer_t *timer))
 * \brief Initialize a timer object.
 *
 * Creates a timer. When created, a timer is left disarmed; it must be
 * started using xntimer_start() in order to be activated.
 *
 * @param timer The address of a timer descriptor the nucleus will use
 * to store the object-specific data.  This descriptor must always be
 * valid while the object is active therefore it must be allocated in
 * permanent memory.
 *
 * @param handler The routine to call upon expiration of the timer.
 *
 * There is no limitation on the number of timers which can be
 * created/active concurrently.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Any kernel context.
 *
 * Rescheduling: never.
 */
#ifdef DOXYGEN_CPP
void xntimer_init(xntimer_t *timer, void (*handler)(xntimer_t *timer));
#endif

void __xntimer_init(xntimer_t *timer, void (*handler) (xntimer_t *timer))
{
	spl_t s;

	xntimerh_init(&timer->aplink);
	xntimerh_date(&timer->aplink) = XN_INFINITE;
	xntimer_set_priority(timer, XNTIMER_STDPRIO);
	timer->status = XNTIMER_DEQUEUED;
	timer->handler = handler;
	timer->interval = 0;
	timer->sched = xnpod_current_sched();

#ifdef CONFIG_XENO_OPT_STATS
	if (!xnpod_current_thread() || xnpod_shadow_p())
		snprintf(timer->name, XNOBJECT_NAME_LEN, "%d/%s",
			 current->pid, current->comm);
	else
		xnobject_copy_name(timer->name,
				   xnpod_current_thread()->name);

	inith(&timer->tblink);
	xnstat_counter_set(&timer->scheduled, 0);
	xnstat_counter_set(&timer->fired, 0);

	xnlock_get_irqsave(&nklock, s);
	appendq(&nkclock.timerq, &timer->tblink);
	xnvfile_touch(&nkclock.vfile);
	xnlock_put_irqrestore(&nklock, s);
#else
	(void)s;
#endif /* CONFIG_XENO_OPT_STATS */
}
EXPORT_SYMBOL_GPL(__xntimer_init);

/*!
 * \fn void xntimer_destroy(xntimer_t *timer)
 *
 * \brief Release a timer object.
 *
 * Destroys a timer. After it has been destroyed, all resources
 * associated with the timer have been released. The timer is
 * automatically deactivated before deletion if active on entry.
 *
 * @param timer The address of a valid timer descriptor.
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

void xntimer_destroy(xntimer_t *timer)
{
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	xntimer_stop(timer);
	__setbits(timer->status, XNTIMER_KILLED);
	timer->sched = NULL;
#ifdef CONFIG_XENO_OPT_STATS
	removeq(&nkclock.timerq, &timer->tblink);
	xnvfile_touch(&nkclock.vfile);
#endif /* CONFIG_XENO_OPT_STATS */
	xnlock_put_irqrestore(&nklock, s);
}
EXPORT_SYMBOL_GPL(xntimer_destroy);

#ifdef CONFIG_SMP

static void xntimer_move(xntimer_t *timer)
{
	xntimer_enqueue(timer);

	if (xntimer_heading_p(timer))
		xntimer_next_remote_shot(timer->sched);
}

/**
 * Migrate a timer.
 *
 * This call migrates a timer to another cpu. In order to avoid pathological
 * cases, it must be called from the CPU to which @a timer is currently
 * attached.
 *
 * @param timer The address of the timer object to be migrated.
 *
 * @param sched The address of the destination CPU xnsched_t structure.
 *
 * @retval -EINVAL if @a timer is queued on another CPU than current ;
 * @retval 0 otherwise.
 *
 */
int xntimer_migrate(xntimer_t *timer, xnsched_t *sched)
{
	int err = 0;
	int queued;
	spl_t s;

	trace_mark(xn_nucleus, timer_migrate, "timer %p cpu %d",
		   timer, (int)xnsched_cpu(sched));

	xnlock_get_irqsave(&nklock, s);

	if (sched == timer->sched)
		goto unlock_and_exit;

	queued = !testbits(timer->status, XNTIMER_DEQUEUED);

	if (queued) {
		if (timer->sched != xnpod_current_sched()) {
			err = -EINVAL;
			goto unlock_and_exit;
		}
		xntimer_stop(timer);
	}

	timer->sched = sched;

	if (queued)
		xntimer_move(timer);

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}
EXPORT_SYMBOL_GPL(xntimer_migrate);

#endif /* CONFIG_SMP */

/**
 * Get the count of overruns for the last tick.
 *
 * This service returns the count of pending overruns for the last tick of a
 * given timer, as measured by the difference between the expected expiry date
 * of the timer and the date @a now passed as argument.
 *
 * @param timer The address of a valid timer descriptor.
 *
 * @param now current date (in the monotonic time base)
 *
 * @return the number of overruns of @a timer at date @a now
 */
unsigned long xntimer_get_overruns(xntimer_t *timer, xnticks_t now)
{
	xnticks_t period = xntimer_interval(timer);
	xnsticks_t delta = now - timer->pexpect;
	unsigned long overruns = 0;

	if (unlikely(delta >= (xnsticks_t) period)) {
		overruns = xnarch_div64(delta, period);
		timer->pexpect += period * overruns;
	}

	timer->pexpect += period;
	return overruns;
}
EXPORT_SYMBOL_GPL(xntimer_get_overruns);

/*!
 * @internal
 * \fn void xntimer_freeze(void)
 *
 * \brief Freeze all timers (from every time bases).
 *
 * This routine deactivates all active timers atomically.
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

void xntimer_freeze(void)
{
	int nr_cpus, cpu;
	spl_t s;

	trace_mark(xn_nucleus, timer_freeze, MARK_NOARGS);

	xnlock_get_irqsave(&nklock, s);

	nr_cpus = xnarch_num_online_cpus();

	for (cpu = 0; cpu < nr_cpus; cpu++) {
		xntimerq_t *timerq = &xnpod_sched_slot(cpu)->timerqueue;
		xntimerh_t *holder;
		while ((holder = xntimerq_head(timerq)) != NULL) {
			__setbits(aplink2timer(holder)->status, XNTIMER_DEQUEUED);
			xntimerq_remove(timerq, holder);
		}
	}

	xnlock_put_irqrestore(&nklock, s);
}
EXPORT_SYMBOL_GPL(xntimer_freeze);

char *xntimer_format_time(xnticks_t value, char *buf, size_t bufsz)
{
	unsigned long ms, us, ns;
	char *p = buf;
	xnticks_t s;

	if (value == 0 && bufsz > 1) {
		strcpy(buf, "-");
		return buf;
	}

	s = xnarch_divrem_billion(value, &ns);
	us = ns / 1000;
	ms = us / 1000;
	us %= 1000;

	if (s)
		p += snprintf(p, bufsz, "%Lus", s);

	if (ms || (s && us))
		p += snprintf(p, bufsz - (p - buf), "%lums", ms);

	if (us)
		p += snprintf(p, bufsz - (p - buf), "%luus", us);

	return buf;
}
EXPORT_SYMBOL_GPL(xntimer_format_time);

#ifdef CONFIG_XENO_OPT_VFILE

#include <nucleus/vfile.h>

static int timer_vfile_show(struct xnvfile_regular_iterator *it, void *data)
{
	const char *tm_status, *wd_status = "";

	if (xnpod_active_p()) {
		tm_status = "on";
#ifdef CONFIG_XENO_OPT_WATCHDOG
		wd_status = "+watchdog";
#endif /* CONFIG_XENO_OPT_WATCHDOG */
	} else
		tm_status = "off";

	xnvfile_printf(it,
		       "status=%s%s:setup=%Lu:clock=%Lu:timerdev=%s:clockdev=%s\n",
		       tm_status, wd_status, xnarch_tsc_to_ns(nktimerlat),
		       xnclock_read_raw(),
		       XNARCH_TIMER_DEVICE, XNARCH_CLOCK_DEVICE);
	return 0;
}

static struct xnvfile_regular_ops timer_vfile_ops = {
	.show = timer_vfile_show,
};

static struct xnvfile_regular timer_vfile = {
	.ops = &timer_vfile_ops,
};

void xntimer_init_proc(void)
{
	xnvfile_init_regular("timer", &timer_vfile, &nkvfroot);
}

void xntimer_cleanup_proc(void)
{
	xnvfile_destroy_regular(&timer_vfile);
}

#endif /* CONFIG_XENO_OPT_VFILE */

/*@}*/

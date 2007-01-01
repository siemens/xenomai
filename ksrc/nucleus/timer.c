/**
 * @file
 * @note Copyright (C) 2001,2002,2003,2007 Philippe Gerum <rpm@xenomai.org>.
 *       Copyright (C) 2004 Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>
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
 * Depending on the time base used, the timer object stores time
 * values either as count of jiffies (periodic), or as count of CPU
 * ticks (aperiodic).
 *
 *@{*/

#include <nucleus/pod.h>
#include <nucleus/thread.h>
#include <nucleus/timer.h>
#include <asm/xenomai/bits/timer.h>

static inline void xntimer_enqueue_aperiodic(xntimer_t *timer)
{
	xntimerq_t *q = &timer->sched->timerqueue;
	xntimerq_insert(q, &timer->aplink);
	__clrbits(timer->status, XNTIMER_DEQUEUED);
}

static inline void xntimer_dequeue_aperiodic(xntimer_t *timer)
{
	xntimerq_remove(&timer->sched->timerqueue, &timer->aplink);
	__setbits(timer->status, XNTIMER_DEQUEUED);
}

static inline void xntimer_next_local_shot(xnsched_t *this_sched)
{
	xntimerh_t *holder = xntimerq_head(&this_sched->timerqueue);
	xnticks_t now, delay, xdate;
	xntimer_t *timer;

	if (!holder)
		return;		/* No pending timer. */

	timer = aplink2timer(holder);

	now = xnarch_get_cpu_tsc();
	xdate = now + nkschedlat + nktimerlat;

	if (xdate >= xntimerh_date(&timer->aplink))
		delay = 0;
	else
		delay = xntimerh_date(&timer->aplink) - xdate;

	xnarch_program_timer_shot(delay <= ULONG_MAX ? delay : ULONG_MAX);
}

static inline int xntimer_heading_p(xntimer_t *timer)
{
	return xntimerq_head(&timer->sched->timerqueue) == &timer->aplink;
}

static inline void xntimer_next_remote_shot(xnsched_t *sched)
{
	xnarch_send_timer_ipi(xnarch_cpumask_of_cpu(xnsched_cpu(sched)));
}

int xntimer_start_aperiodic(xntimer_t *timer,
			    xnticks_t value, xnticks_t interval,
			    int mode)
{
	xnticks_t date, now;

	if (!testbits(timer->status, XNTIMER_DEQUEUED))
		xntimer_dequeue_aperiodic(timer);

	now = xnarch_get_cpu_tsc();

	if (mode == XN_RELATIVE)
		date = xnarch_ns_to_tsc(value) + now;
	else {
		date = xnarch_ns_to_tsc(value - nktbase.wallclock_offset);
		if (date <= now)
			return -ETIMEDOUT;
	}

	xntimerh_date(&timer->aplink) = date;
	timer->interval = xnarch_ns_to_tsc(interval);

	xntimer_enqueue_aperiodic(timer);
	if (xntimer_heading_p(timer)) {
		if (xntimer_sched(timer) != xnpod_current_sched())
			xntimer_next_remote_shot(xntimer_sched(timer));
		else
			xntimer_next_local_shot(xntimer_sched(timer));
	}

	return 0;
}

void xntimer_stop_aperiodic(xntimer_t *timer)
{
	int heading;

	heading = xntimer_heading_p(timer);
	xntimer_dequeue_aperiodic(timer);

	/* If we removed the heading timer, reprogram the next shot if
	   any. If the timer was running on another CPU, let it tick. */
	if (heading && xntimer_sched(timer) == xnpod_current_sched())
		xntimer_next_local_shot(xntimer_sched(timer));
}

xnticks_t xntimer_get_date_aperiodic(xntimer_t *timer)
{
	return xnarch_tsc_to_ns(xntimerh_date(&timer->aplink));
}

xnticks_t xntimer_get_timeout_aperiodic(xntimer_t *timer)
{
	xnticks_t tsc = xnarch_get_cpu_tsc();

	if (xntimerh_date(&timer->aplink) < tsc)
		return 1;	/* Will elapse shortly. */

	return xnarch_tsc_to_ns(xntimerh_date(&timer->aplink) - tsc);
}

xnticks_t xntimer_get_interval_aperiodic(xntimer_t *timer)
{
	return xnarch_tsc_to_ns(timer->interval);
}

xnticks_t xntimer_get_raw_expiry_aperiodic(xntimer_t *timer)
{
	return xntimerh_date(&timer->aplink);
}

/*!
 * @internal
 * \fn void xntimer_tick_aperiodic(void)
 *
 * \brief Process a timer tick for the aperiodic master time base.
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

void xntimer_tick_aperiodic(void)
{
	xnsched_t *sched = xnpod_current_sched();
	xntimerq_t *timerq = &sched->timerqueue;
	xntimerh_t *holder;
	xntimer_t *timer;
	xnticks_t now;

	while ((holder = xntimerq_head(timerq)) != NULL) {
		timer = aplink2timer(holder);

		now = xnarch_get_cpu_tsc();
		if (xntimerh_date(&timer->aplink) - nkschedlat > now)
			/* No need to continue in aperiodic mode since timeout
			   dates are ordered by increasing values. */
			break;

		xntimer_dequeue_aperiodic(timer);

		if (timer != &nkpod->htimer) {
			if (!testbits(nktbase.status, XNTBLCK)) {
				timer->handler(timer);

				if (timer->interval == XN_INFINITE ||
				    !testbits(timer->status, XNTIMER_DEQUEUED)
				    || testbits(timer->status, XNTIMER_KILLED))
					/* The elapsed timer has no reload value, or has
					   been re-enqueued likely as a result of a call
					   to xntimer_start() from the timeout handler, or
					   has been killed by the handler. In all cases,
					   don't attempt to re-enqueue it for the next
					   shot. */
					continue;
			} else if (timer->interval == XN_INFINITE) {
				xntimerh_date(&timer->aplink) +=
				    nkpod->htimer.interval;
				continue;
			}
		} else
			/* By postponing the propagation of the low-priority host
			   tick to the interrupt epilogue (see
			   xnintr_irq_handler()), we save some I-cache, which
			   translates into precious microsecs on low-end hw. */
			__setbits(sched->status, XNHTICK);

		while ((xntimerh_date(&timer->aplink) += timer->interval) < now)
		    ;
		xntimer_enqueue_aperiodic(timer);
	}

	xntimer_next_local_shot(sched);
}

static void xntimer_move_aperiodic(xntimer_t *timer)
{
	xntimer_enqueue_aperiodic(timer);

	if (xntimer_heading_p(timer))
		xntimer_next_remote_shot(timer->sched);
}

#ifdef CONFIG_XENO_OPT_TIMING_PERIODIC

static inline void xntimer_enqueue_periodic(xntimer_t *timer)
{
	unsigned slot = (xntlholder_date(&timer->plink) & XNTIMER_WHEELMASK);
	unsigned cpu = xnsched_cpu(timer->sched);
	struct percpu_cascade *pc = &base2slave(timer->base)->cascade[cpu];
	/* Just prepend the new timer to the proper slot. */
	xntlist_insert(&pc->wheel[slot], &timer->plink);
	__clrbits(timer->status, XNTIMER_DEQUEUED);
}

static inline void xntimer_dequeue_periodic(xntimer_t *timer)
{
	unsigned slot = (xntlholder_date(&timer->plink) & XNTIMER_WHEELMASK);
	unsigned cpu = xnsched_cpu(timer->sched);
	struct percpu_cascade *pc = &base2slave(timer->base)->cascade[cpu];
	xntlist_remove(&pc->wheel[slot], &timer->plink);
	__setbits(timer->status, XNTIMER_DEQUEUED);
}

static int xntimer_start_periodic(xntimer_t *timer,
				  xnticks_t value, xnticks_t interval,
				  int mode)
{
	if (!testbits(timer->status, XNTIMER_DEQUEUED))
		xntimer_dequeue_periodic(timer);

	if (mode == XN_RELATIVE)
		value += timer->base->jiffies;
	else {
		value -= timer->base->wallclock_offset;
		if (value <= timer->base->jiffies)
			return -ETIMEDOUT;
	}

	xntlholder_date(&timer->plink) = value;
	timer->interval = interval;

	xntimer_enqueue_periodic(timer);

	return 0;
}

static void xntimer_stop_periodic(xntimer_t *timer)
{
	xntimer_dequeue_periodic(timer);
}

static xnticks_t xntimer_get_date_periodic(xntimer_t *timer)
{
	return xntlholder_date(&timer->plink);
}

static xnticks_t xntimer_get_timeout_periodic(xntimer_t *timer)
{
	return xntlholder_date(&timer->plink) - timer->base->jiffies;
}

static xnticks_t xntimer_get_interval_periodic(xntimer_t *timer)
{
	return timer->interval;
}

static xnticks_t xntimer_get_raw_expiry_periodic(xntimer_t *timer)
{
	return xntlholder_date(&timer->plink);
}

static void xntimer_move_periodic(xntimer_t *timer)
{
	xntimer_enqueue_periodic(timer);
}

/*!
 * @internal
 * \fn void xntimer_tick_periodic(xntimer_t *mtimer)
 *
 * \brief Process a timer tick for a slave periodic time base.
 *
 * The periodic timer tick is cascaded from a software timer managed
 * from the master aperiodic time base; in other words, periodic
 * timing is emulated by software timers running in aperiodic timing
 * mode. There may be several concurrent periodic time bases (albeit a
 * single aperiodic time base - i.e. the master one called "nktbase" -
 * may exist at any point in time).
 *
 * This routine informs all active timers that the clock has been
 * updated by processing the timer wheel. Elapsed timer actions will
 * be fired.
 *
 * @param mtimer The address of the cascading timer running in the
 * master time base which announced the tick.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Interrupt service routine, nklock locked, interrupts off
 *
 * Rescheduling: never.
 *
 * @note Only active timers are inserted into the timer wheel.
 */

void xntimer_tick_periodic(xntimer_t *mtimer)
{
	xntslave_t *slave = timer2slave(mtimer);
	xnsched_t *sched = xnpod_current_sched();
	xntbase_t *base = &slave->base;
	xntlholder_t *holder;
	xnqueue_t *timerq;
	xntimer_t *timer;

	/* Update the periodic clocks keeping the things strictly
	   monotonous (this routine is run on every cpu, but only CPU
	   XNTIMER_KEEPER_ID should do this). */
	if (sched == xnpod_sched_slot(XNTIMER_KEEPER_ID))
		++base->jiffies;

	timerq = &slave->cascade[xnsched_cpu(sched)].wheel[base->jiffies & XNTIMER_WHEELMASK];

	while ((holder = xntlist_head(timerq)) != NULL) {
		timer = plink2timer(holder);

		if (xntlholder_date(&timer->plink) > base->jiffies)
			break;

		xntimer_dequeue_periodic(timer);

		timer->handler(timer);

		if (timer->interval == XN_INFINITE ||
		    !testbits(timer->status, XNTIMER_DEQUEUED)
		    || testbits(timer->status, XNTIMER_KILLED))
			continue;

		xntlholder_date(&timer->plink) = base->jiffies + timer->interval;
		xntimer_enqueue_periodic(timer);
	}

	xnpod_do_rr();		/* Do round-robin management. */

	if (base->hook)
		base->hook();
}

void xntslave_init(xntslave_t *slave)
{
	int nr_cpus, cpu, n;

	for (cpu = 0, nr_cpus = xnarch_num_online_cpus(); cpu < nr_cpus; cpu++) {

		struct percpu_cascade *pc = &slave->cascade[cpu];

		for (n = 0; n < XNTIMER_WHEELSIZE; n++)
			xntlist_init(&pc->wheel[n]);

		/* Slave periodic time bases are cascaded from the
		 * master aperiodic time base. */
		xntimer_init(&pc->timer, &nktbase, &xntimer_tick_periodic);
		xntimer_set_priority(&pc->timer, XNTIMER_HIPRIO);
		xntimer_set_sched(&pc->timer, xnpod_sched_slot(cpu));
	}
}

void xntslave_destroy(xntslave_t *slave)
{
	int nr_cpus, cpu, n;
	spl_t s;

	for (cpu = 0, nr_cpus = xnarch_num_online_cpus(); cpu < nr_cpus; cpu++) {

		struct percpu_cascade *pc = &slave->cascade[cpu];

		xnlock_get_irqsave(&nklock, s);
		xntimer_destroy(&pc->timer);
		xnlock_put_irqrestore(&nklock, s);

		for (n = 0; n < XNTIMER_WHEELSIZE; n++) {

			xnqueue_t *timerq = &pc->wheel[n];
			xntlholder_t *holder;

			while ((holder = xntlist_head(timerq)) != NULL) {
				__setbits(plink2timer(holder)->status, XNTIMER_DEQUEUED);
				xntlist_remove(timerq, holder);
			}
		}
	}
}

void xntslave_update(xntslave_t *slave, xnticks_t interval)
{
	int nr_cpus, cpu;

	for (cpu = 0, nr_cpus = xnarch_num_online_cpus(); cpu < nr_cpus; cpu++) {

		struct percpu_cascade *pc = &slave->cascade[cpu];
		xntimer_interval(&pc->timer) = interval;
	}
}

void xntslave_start(xntslave_t *slave, xnticks_t interval)
{
	int nr_cpus, cpu;
	spl_t s;

	for (cpu = 0, nr_cpus = xnarch_num_online_cpus(); cpu < nr_cpus; cpu++) {

		struct percpu_cascade *pc = &slave->cascade[cpu];
		xnlock_get_irqsave(&nklock, s);
		xntimer_start(&pc->timer, interval, interval, XN_RELATIVE);
		xnlock_put_irqrestore(&nklock, s);
	}
}

void xntslave_stop(xntslave_t *slave)
{
	int nr_cpus, cpu;
	spl_t s;

	for (cpu = 0, nr_cpus = xnarch_num_online_cpus(); cpu < nr_cpus; cpu++) {

		struct percpu_cascade *pc = &slave->cascade[cpu];
		xnlock_get_irqsave(&nklock, s);
		xntimer_stop(&pc->timer);
		xnlock_put_irqrestore(&nklock, s);
	}
}

xntbops_t nktimer_ops_periodic = {

	.start_timer = &xntimer_start_periodic,
	.stop_timer = &xntimer_stop_periodic,
	.get_timer_date = &xntimer_get_date_periodic,
	.get_timer_timeout = &xntimer_get_timeout_periodic,
	.get_timer_interval = &xntimer_get_interval_periodic,
	.get_timer_raw_expiry = &xntimer_get_raw_expiry_periodic,
	.move_timer = &xntimer_move_periodic,
};

#endif /* CONFIG_XENO_OPT_TIMING_PERIODIC */

/*! 
 * \fn void xntimer_init(xntimer_t *timer,xntbase_t *base,void (*handler)(xntimer_t *timer))
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
 * @param base The descriptor address of the time base the new timer
 * depends on. See xntbase_alloc() for detailed explanations about
 * time bases.
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
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

void xntimer_init(xntimer_t *timer, xntbase_t *base, void (*handler) (xntimer_t *timer))
{
	/* CAUTION: Setup from xntimer_init() must not depend on the
	   periodic/aperiodic timing mode. */

	xntimerh_init(&timer->aplink);
	xntimerh_date(&timer->aplink) = XN_INFINITE;
#ifdef CONFIG_XENO_OPT_TIMING_PERIODIC
	timer->base = base;
	xntlholder_init(&timer->plink);
	xntlholder_date(&timer->plink) = XN_INFINITE;
#endif /* CONFIG_XENO_OPT_TIMING_PERIODIC */
	xntimer_set_priority(timer, XNTIMER_STDPRIO);
	timer->status = XNTIMER_DEQUEUED;
	timer->handler = handler;
	timer->interval = 0;
	timer->sched = xnpod_current_sched();

	xnarch_init_display_context(timer);
}

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
	xnlock_put_irqrestore(&nklock, s);
}

#ifdef CONFIG_SMP
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

	xnlock_get_irqsave(&nklock, s);

	if (sched == timer->sched)
		goto unlock_and_exit;

	queued = !testbits(timer->status, XNTIMER_DEQUEUED);

	/* Avoid the pathological case where the timer interrupt did not occur yet
	   for the current date on the timer source CPU, whereas we are trying to
	   migrate it to a CPU where the timer interrupt already occured. This would
	   not be a problem in aperiodic mode. */

	if (queued) {

		if (timer->sched != xnpod_current_sched()) {
			err = -EINVAL;
			goto unlock_and_exit;
		}

#ifdef CONFIG_XENO_OPT_TIMING_PERIODIC
		timer->base->ops->stop_timer(timer);
#else /* !CONFIG_XENO_OPT_TIMING_PERIODIC */
		xntimer_stop_aperiodic(timer);
#endif /* !CONFIG_XENO_OPT_TIMING_PERIODIC */
	}

	timer->sched = sched;

	if (queued)
#ifdef CONFIG_XENO_OPT_TIMING_PERIODIC
		timer->base->ops->move_timer(timer);
#else /* !CONFIG_XENO_OPT_TIMING_PERIODIC */
		xntimer_move_aperiodic(timer);
#endif /* !CONFIG_XENO_OPT_TIMING_PERIODIC */

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}
#endif /* CONFIG_SMP */

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

	xnlock_get_irqsave(&nklock, s);

	nr_cpus = xnarch_num_online_cpus();

	for (cpu = 0; cpu < nr_cpus; cpu++) {

		xntimerq_t *timerq = &xnpod_sched_slot(cpu)->timerqueue;
		xntimerh_t *holder;

		while ((holder = xntimerq_head(timerq)) != NULL) {
			__setbits(aplink2timer(holder)->status, XNTIMER_DEQUEUED);
			xntimerq_remove(timerq, holder);
		}

		/* Dequeuing all timers from the master time base
		 * freezes all slave time bases the same way, so there
		 * is no need to handle anything more here. */
	}

	xnlock_put_irqrestore(&nklock, s);
}

xntbops_t nktimer_ops_aperiodic = {

	.start_timer = &xntimer_start_aperiodic,
	.stop_timer = &xntimer_stop_aperiodic,
	.get_timer_date = &xntimer_get_date_aperiodic,
	.get_timer_timeout = &xntimer_get_timeout_aperiodic,
	.get_timer_interval = &xntimer_get_interval_aperiodic,
	.get_timer_raw_expiry = &xntimer_get_raw_expiry_aperiodic,
	.move_timer = &xntimer_move_aperiodic,
};

/*@}*/

EXPORT_SYMBOL(xntimer_start_aperiodic);
EXPORT_SYMBOL(xntimer_stop_aperiodic);
EXPORT_SYMBOL(xntimer_get_date_aperiodic);
EXPORT_SYMBOL(xntimer_get_timeout_aperiodic);
EXPORT_SYMBOL(xntimer_get_interval_aperiodic);
EXPORT_SYMBOL(xntimer_get_raw_expiry_aperiodic);
EXPORT_SYMBOL(xntimer_init);
EXPORT_SYMBOL(xntimer_destroy);
EXPORT_SYMBOL(xntimer_freeze);
EXPORT_SYMBOL(xntimer_get_date);
EXPORT_SYMBOL(xntimer_get_timeout);
#ifdef CONFIG_SMP
EXPORT_SYMBOL(xntimer_migrate);
#endif /* CONFIG_SMP */

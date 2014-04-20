/**
 * @file
 * @note Copyright (C) 2001,2002,2003,2007,2012 Philippe Gerum <rpm@xenomai.org>.
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
 * The Xenomai timer facility depends on a Xenomai clock source
 * (xnclock) for scheduling the next activation times.
 *
 * The Xenomai core provides and depends on a monotonic clock source
 * (nkclock) with nanosecond resolution, driving the platform timer
 * hardware exposed by the interrupt pipeline.
 *
 *@{*/

#include <linux/ipipe.h>
#include <linux/ipipe_tickdev.h>
#include <linux/sched.h>
#include <cobalt/kernel/sched.h>
#include <cobalt/kernel/thread.h>
#include <cobalt/kernel/timer.h>
#include <cobalt/kernel/intr.h>
#include <cobalt/kernel/clock.h>
#include <cobalt/kernel/trace.h>
#include <cobalt/kernel/arith.h>
#include <trace/events/cobalt-core.h>

int xntimer_heading_p(struct xntimer *timer)
{
	struct xnsched *sched = timer->sched;
	xntimerq_it_t it;
	xntimerq_t *q;
	xntimerh_t *h;

	q = xntimer_percpu_queue(timer);
	h = xntimerq_it_begin(q, &it);
	if (h == &timer->aplink)
		return 1;

	if (sched->lflags & XNHDEFER) {
		h = xntimerq_it_next(q, &it, h);
		if (h == &timer->aplink)
			return 1;
	}

	return 0;
}

/*!
 * @fn void xntimer_start(struct xntimer *timer,xnticks_t value,xnticks_t interval,
 *                        xntmode_t mode)
 * @brief Arm a timer.
 *
 * Activates a timer so that the associated timeout handler will be
 * fired after each expiration time. A timer can be either periodic or
 * one-shot, depending on the reload value passed to this routine. The
 * given timer must have been previously initialized.
 *
 * A timer is attached to the clock specified in xntimer_init().
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
 * is based on the adjustable real-time date for the relevant clock
 * (obtained from xnclock_read_realtime()).
 *
 * @return 0 is returned upon success, or -ETIMEDOUT if an absolute
 * date in the past has been given.
 *
 * @remark Tags: atomic-entry.
 */
int xntimer_start(struct xntimer *timer,
		  xnticks_t value, xnticks_t interval,
		  xntmode_t mode)
{
	struct xnclock *clock = xntimer_clock(timer);
	xntimerq_t *q = xntimer_percpu_queue(timer);
	struct xnsched *sched;
	xnticks_t date, now;

	trace_cobalt_timer_start(timer, value, interval, mode);

	if ((timer->status & XNTIMER_DEQUEUED) == 0)
		xntimer_dequeue(timer, q);

	now = xnclock_read_raw(clock);

	timer->status &= ~(XNTIMER_REALTIME | XNTIMER_FIRED | XNTIMER_PERIODIC);
	switch (mode) {
	case XN_RELATIVE:
		if ((xnsticks_t)value < 0)
			return -ETIMEDOUT;
		date = xnclock_ns_to_ticks(clock, value) + now;
		break;
	case XN_REALTIME:
		timer->status |= XNTIMER_REALTIME;
		value -= xnclock_get_offset(clock);
		/* fall through */
	default: /* XN_ABSOLUTE || XN_REALTIME */
		date = xnclock_ns_to_ticks(clock, value);
		if ((xnsticks_t)(date - now) <= 0)
			return -ETIMEDOUT;
		break;
	}

	xntimerh_date(&timer->aplink) = date;

	timer->interval = XN_INFINITE;
	if (interval != XN_INFINITE) {
		timer->interval = xnclock_ns_to_ticks(clock, interval);
		timer->pexpect = date;
		timer->status |= XNTIMER_PERIODIC;
	}

	xntimer_enqueue(timer, q);
	if (xntimer_heading_p(timer)) {
		sched = xntimer_sched(timer);
		if (sched != xnsched_current())
			xnclock_remote_shot(clock, sched);
		else
			xnclock_program_shot(clock, sched);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(xntimer_start);

/**
 * @fn int xntimer_stop(struct xntimer *timer)
 *
 * @brief Disarm a timer.
 *
 * This service deactivates a timer previously armed using
 * xntimer_start(). Once disarmed, the timer can be subsequently
 * re-armed using the latter service.
 *
 * @param timer The address of a valid timer descriptor.
 *
 * @remark Tags: atomic-entry, isr-allowed.
 */
void __xntimer_stop(struct xntimer *timer)
{
	struct xnclock *clock = xntimer_clock(timer);
	xntimerq_t *q = xntimer_percpu_queue(timer);
	struct xnsched *sched;
	int heading;

	trace_cobalt_timer_stop(timer);

	heading = xntimer_heading_p(timer);
	xntimer_dequeue(timer, q);
	timer->status &= ~XNTIMER_FIRED;
	sched = xntimer_sched(timer);

	/*
	 * If we removed the heading timer, reprogram the next shot if
	 * any. If the timer was running on another CPU, let it tick.
	 */
	if (heading && sched == xnsched_current())
		xnclock_program_shot(clock, sched);
}
EXPORT_SYMBOL_GPL(__xntimer_stop);

/**
 * @fn xnticks_t xntimer_get_date(struct xntimer *timer)
 *
 * @brief Return the absolute expiration date.
 *
 * Return the next expiration date of a timer as an absolute count of
 * nanoseconds.
 *
 * @param timer The address of a valid timer descriptor.
 *
 * @return The expiration date in nanoseconds. The special value
 * XN_INFINITE is returned if @a timer is currently disabled.
 *
 * @remark Tags: isr-allowed.
 */
xnticks_t xntimer_get_date(struct xntimer *timer)
{
	if (!xntimer_running_p(timer))
		return XN_INFINITE;

	return xnclock_ticks_to_ns(xntimer_clock(timer),
				   xntimerh_date(&timer->aplink));
}
EXPORT_SYMBOL_GPL(xntimer_get_date);

/**
 * @fn xnticks_t xntimer_get_timeout(struct xntimer *timer)
 *
 * @brief Return the relative expiration date.
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
 * @remark Tags: irqs-off, isr-allowed.
 */
xnticks_t xntimer_get_timeout(struct xntimer *timer)
{
	xnticks_t ticks, delta;
	struct xnclock *clock;

	if (!xntimer_running_p(timer))
		return XN_INFINITE;

	clock = xntimer_clock(timer);
	ticks = xnclock_read_raw(clock);
	if (xntimerh_date(&timer->aplink) < ticks)
		return 1;	/* Will elapse shortly. */

	delta = xntimerh_date(&timer->aplink) - ticks;

	return xnclock_ticks_to_ns(clock, delta);
}
EXPORT_SYMBOL_GPL(xntimer_get_timeout);

/**
 * @fn xnticks_t xntimer_get_interval(struct xntimer *timer)
 *
 * @brief Return the timer interval value.
 *
 * Return the timer interval value in nanoseconds.
 *
 * @param timer The address of a valid timer descriptor.
 *
 * @return The duration of a period in nanoseconds. The special value
 * XN_INFINITE is returned if @a timer is currently disabled or
 * one shot.
 *
 * @remark Tags: isr-allowed.
 */
xnticks_t xntimer_get_interval(struct xntimer *timer)
{
	struct xnclock *clock = xntimer_clock(timer);

	return xnclock_ticks_to_ns_rounded(clock, timer->interval);
}
EXPORT_SYMBOL_GPL(xntimer_get_interval);

/**
 * @fn void xntimer_init(struct xntimer *timer,struct xnclock *clock,void (*handler)(struct xntimer *timer), struct xnthread *thread)
 * @brief Initialize a timer object.
 *
 * Creates a timer. When created, a timer is left disarmed; it must be
 * started using xntimer_start() in order to be activated.
 *
 * @param timer The address of a timer descriptor the nucleus will use
 * to store the object-specific data.  This descriptor must always be
 * valid while the object is active therefore it must be allocated in
 * permanent memory.
 *
 * @param clock The clock the timer relates to. Xenomai defines a
 * monotonic system clock, with nanosecond resolution, named
 * nkclock. In addition, external clocks driven by other tick sources
 * may be created dynamically if CONFIG_XENO_OPT_EXTCLOCK is defined.
 *
 * @param handler The routine to call upon expiration of the timer.
 *
 * @param thread The optional thread object the new timer is affine
 * to. If non-NULL, the timer will fire on the same CPU @a thread
 * currently runs on by default. A call to xntimer_set_sched() may
 * change this setting.
 *
 * There is no limitation on the number of timers which can be
 * created/active concurrently.
 *
 * @remark Tags: none.
 */
#ifdef DOXYGEN_CPP
void xntimer_init(struct xntimer *timer, struct xnclock *clock,
		  void (*handler)(struct xntimer *timer),
		  struct xnthread *thread);
#endif

void __xntimer_init(struct xntimer *timer,
		    struct xnclock *clock,
		    void (*handler)(struct xntimer *timer),
		    struct xnthread *thread)
{
	spl_t s __maybe_unused;
	int cpu;

#ifdef CONFIG_XENO_OPT_EXTCLOCK
	timer->clock = clock;
#endif
	xntimerh_init(&timer->aplink);
	xntimerh_date(&timer->aplink) = XN_INFINITE;
	xntimer_set_priority(timer, XNTIMER_STDPRIO);
	timer->status = XNTIMER_DEQUEUED;
	timer->handler = handler;
	timer->interval = 0;
	/*
	 * Timers have to run on a real-time CPU, i.e. a member of the
	 * xnsched_realtime_cpus mask. If the new timer is affine to a
	 * thread, we assign it the same CPU (which has to be correct),
	 * otherwise pick the first valid real-time CPU by default.
	 */
	if (thread)
		timer->sched = thread->sched;
	else {
		cpu = first_cpu(xnsched_realtime_cpus);
		timer->sched = xnsched_struct(cpu);
	}

#ifdef CONFIG_XENO_OPT_STATS
#ifdef CONFIG_XENO_OPT_EXTCLOCK
	timer->tracker = clock;
#endif
	ksformat(timer->name, XNOBJECT_NAME_LEN, "%d/%s",
		 current->pid, current->comm);
	xntimer_reset_stats(timer);
	xnlock_get_irqsave(&nklock, s);
	list_add_tail(&timer->next_stat, &clock->statq);
	clock->nrtimers++;
	xnvfile_touch(&clock->vfile);
	xnlock_put_irqrestore(&nklock, s);
#endif /* CONFIG_XENO_OPT_STATS */
}
EXPORT_SYMBOL_GPL(__xntimer_init);

#if defined(CONFIG_XENO_OPT_EXTCLOCK) && defined(CONFIG_XENO_OPT_STATS)

void xntimer_switch_tracking(struct xntimer *timer,
			     struct xnclock *newclock)
{
	struct xnclock *oldclock = timer->tracker;
	spl_t s;
	
	xnlock_get_irqsave(&nklock, s);
	list_del(&timer->next_stat);
	oldclock->nrtimers--;
	xnvfile_touch(&oldclock->vfile);
	list_add_tail(&timer->next_stat, &newclock->statq);
	newclock->nrtimers++;
	xnvfile_touch(&newclock->vfile);
	timer->tracker = newclock;
	xnlock_put_irqrestore(&nklock, s);
}
EXPORT_SYMBOL_GPL(xntimer_switch_tracking);

#endif /* CONFIG_XENO_OPT_EXTCLOCK && CONFIG_XENO_OPT_STATS */

/**
 * @fn void xntimer_destroy(struct xntimer *timer)
 *
 * @brief Release a timer object.
 *
 * Destroys a timer. After it has been destroyed, all resources
 * associated with the timer have been released. The timer is
 * automatically deactivated before deletion if active on entry.
 *
 * @param timer The address of a valid timer descriptor.
 *
 * @remark Tags: none.
 */
void xntimer_destroy(struct xntimer *timer)
{
	struct xnclock *clock __maybe_unused = xntimer_clock(timer);
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	xntimer_stop(timer);
	timer->status |= XNTIMER_KILLED;
	timer->sched = NULL;
#ifdef CONFIG_XENO_OPT_STATS
	list_del(&timer->next_stat);
	clock->nrtimers--;
	xnvfile_touch(&clock->vfile);
#endif /* CONFIG_XENO_OPT_STATS */
	xnlock_put_irqrestore(&nklock, s);
}
EXPORT_SYMBOL_GPL(xntimer_destroy);

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
 * @param sched The address of the destination per-CPU scheduler
 * slot.
 *
 * @remark Tags: atomic-entry.
 */
void __xntimer_migrate(struct xntimer *timer, struct xnsched *sched)
{				/* nklocked, IRQs off */
	struct xnclock *clock;
	xntimerq_t *q;

	if (sched == timer->sched)
		return;

	trace_cobalt_timer_migrate(timer, xnsched_cpu(sched));

	if (timer->status & XNTIMER_DEQUEUED)
		timer->sched = sched;
	else {
		xntimer_stop(timer);
		timer->sched = sched;
		clock = xntimer_clock(timer);
		q = xntimer_percpu_queue(timer);
		xntimer_enqueue(timer, q);
		if (xntimer_heading_p(timer))
			xnclock_remote_shot(clock, sched);
	}
}
EXPORT_SYMBOL_GPL(__xntimer_migrate);

int xntimer_setup_ipi(void)
{
	return ipipe_request_irq(&xnsched_realtime_domain,
				 IPIPE_HRTIMER_IPI,
				 (ipipe_irq_handler_t)xnintr_core_clock_handler,
				 NULL, NULL);
}

void xntimer_release_ipi(void)
{
	ipipe_free_irq(&xnsched_realtime_domain, IPIPE_HRTIMER_IPI);
}

#endif /* CONFIG_SMP */

/**
 * Get the count of overruns for the last tick.
 *
 * This service returns the count of pending overruns for the last
 * tick of a given timer, as measured by the difference between the
 * expected expiry date of the timer and the date @a now passed as
 * argument.
 *
 * @param timer The address of a valid timer descriptor.
 *
 * @param now current date (as
 * xnclock_read_raw(xntimer_clock(timer)))
 *
 * @return the number of overruns of @a timer at date @a now
 *
 * @remark Tags: irqs-off, isr-allowed.
 */
unsigned long long xntimer_get_overruns(struct xntimer *timer, xnticks_t now)
{
	xnticks_t period = xntimer_interval(timer);
	xnsticks_t delta = now - timer->pexpect;
	unsigned long long overruns = 0;

	if (unlikely(delta >= (xnsticks_t) period)) {
		overruns = xnarch_div64(delta, period);
		timer->pexpect += period * overruns;
	}

	timer->pexpect += period;
	return overruns;
}
EXPORT_SYMBOL_GPL(xntimer_get_overruns);

char *xntimer_format_time(xnticks_t ns, char *buf, size_t bufsz)
{
	unsigned long ms, us, rem;
	int len = (int)bufsz;
	char *p = buf;
	xnticks_t sec;

	if (ns == 0 && bufsz > 1) {
		strcpy(buf, "-");
		return buf;
	}

	sec = xnclock_divrem_billion(ns, &rem);
	us = rem / 1000;
	ms = us / 1000;
	us %= 1000;

	if (sec) {
		p += ksformat(p, bufsz, "%Lus", sec);
		len = bufsz - (p - buf);
	}

	if (len > 0 && (ms || (sec && us))) {
		p += ksformat(p, bufsz - (p - buf), "%lums", ms);
		len = bufsz - (p - buf);
	}

	if (len > 0 && us)
		p += ksformat(p, bufsz - (p - buf), "%luus", us);

	return buf;
}
EXPORT_SYMBOL_GPL(xntimer_format_time);

/**
 * @internal
 * @fn static int program_htick_shot(unsigned long delay, struct clock_event_device *cdev)
 *
 * @brief Program next host tick as a Xenomai timer event.
 *
 * Program the next shot for the host tick on the current CPU.
 * Emulation is done using a nucleus timer attached to the master
 * timebase.
 *
 * @param delay The time delta from the current date to the next tick,
 * expressed as a count of nanoseconds.
 *
 * @param cdev An pointer to the clock device which notifies us.
 *
 * Environment:
 *
 * This routine is a callback invoked from the kernel's clock event
 * handlers.
 *
 * @note GENERIC_CLOCKEVENTS is required from the host kernel.
 *
 * @remark Tags: none.
 */
static int program_htick_shot(unsigned long delay,
			      struct clock_event_device *cdev)
{
	struct xnsched *sched;
	int ret;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	sched = xnsched_current();
	ret = xntimer_start(&sched->htimer, delay, XN_INFINITE, XN_RELATIVE);
	xnlock_put_irqrestore(&nklock, s);

	return ret ? -ETIME : 0;
}

/**
 * @internal
 * @fn void switch_htick_mode(enum clock_event_mode mode, struct clock_event_device *cdev)
 *
 * @brief Tick mode switch emulation callback.
 *
 * Changes the host tick mode for the tick device of the current CPU.
 *
 * @param mode The new mode to switch to. The possible values are:
 *
 * - CLOCK_EVT_MODE_ONESHOT, for a switch to oneshot mode.
 *
 * - CLOCK_EVT_MODE_PERIODIC, for a switch to periodic mode. The current
 * implementation for the generic clockevent layer Linux exhibits
 * should never downgrade from a oneshot to a periodic tick mode, so
 * this mode should not be encountered. This said, the associated code
 * is provided, basically for illustration purposes.
 *
 * - CLOCK_EVT_MODE_SHUTDOWN, indicates the removal of the current
 * tick device. Normally, the nucleus only interposes on tick devices
 * which should never be shut down, so this mode should not be
 * encountered.
 *
 * @param cdev An opaque pointer to the clock device which notifies us.
 *
 * @remark Tags: none.
 *
 * @note GENERIC_CLOCKEVENTS is required from the host kernel.
 */
static void switch_htick_mode(enum clock_event_mode mode,
			      struct clock_event_device *cdev)
{
	struct xnsched *sched;
	xnticks_t tickval;
	spl_t s;

	if (mode == CLOCK_EVT_MODE_ONESHOT)
		return;

	xnlock_get_irqsave(&nklock, s);

	sched = xnsched_current();

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		tickval = 1000000000UL / HZ;
		xntimer_start(&sched->htimer, tickval, tickval, XN_RELATIVE);
		break;
	case CLOCK_EVT_MODE_SHUTDOWN:
		xntimer_stop(&sched->htimer);
		break;
	default:
		XENO_BUGON(NUCLEUS, 1);
	}

	xnlock_put_irqrestore(&nklock, s);
}

/**
 * @fn int xntimer_grab_hardware(int cpu)
 * @brief Grab the hardware timer.
 *
 * xntimer_grab_hardware() grabs and tunes the hardware timer in oneshot
 * mode in order to clock the master time base. GENERIC_CLOCKEVENTS is
 * required from the host kernel.
 *
 * Host tick emulation is performed for sharing the clockchip hardware
 * between Linux and Xenomai, when the former provides support for
 * oneshot timing (i.e. high resolution timers and no-HZ scheduler
 * ticking).
 *
 * @param cpu The CPU number to grab the timer from.
 *
 * @return a positive value is returned on success, representing the
 * duration of a Linux periodic tick expressed as a count of
 * nanoseconds; zero should be returned when the Linux kernel does not
 * undergo periodic timing on the given CPU (e.g. oneshot
 * mode). Otherwise:
 *
 * - -EBUSY is returned if the hardware timer has already been
 * grabbed.  xntimer_release_hardware() must be issued before
 * xntimer_grab_hardware() is called again.
 *
 * - -ENODEV is returned if the hardware timer cannot be used.  This
 * situation may occur after the kernel disabled the timer due to
 * invalid calibration results; in such a case, such hardware is
 * unusable for any timing duties.
 *
 * @remark Tags: secondary-only.
 */
int xntimer_grab_hardware(int cpu)
{
	int tickval, ret;

	ret = ipipe_timer_start(xnintr_core_clock_handler,
				switch_htick_mode, program_htick_shot, cpu);
	switch (ret) {
	case CLOCK_EVT_MODE_PERIODIC:
		/* oneshot tick emulation callback won't be used, ask
		 * the caller to start an internal timer for emulating
		 * a periodic tick. */
		tickval = 1000000000UL / HZ;
		break;

	case CLOCK_EVT_MODE_ONESHOT:
		/* oneshot tick emulation */
		tickval = 1;
		break;

	case CLOCK_EVT_MODE_UNUSED:
		/* we don't need to emulate the tick at all. */
		tickval = 0;
		break;

	case CLOCK_EVT_MODE_SHUTDOWN:
		return -ENODEV;

	default:
		return ret;
	}

	return tickval;
}

/**
 * @fn void xntimer_release_hardware(int cpu)
 * @brief Release the hardware timer.
 *
 * Releases the hardware timer, thus reverting the effect of a
 * previous call to xntimer_grab_hardware(). In case the timer
 * hardware is shared with Linux, a periodic setup suitable for the
 * Linux kernel is reset.
 *
 * @param cpu The CPU number the timer was grabbed from.
 *
 * @remark Tags: secondary-only.
 */
void xntimer_release_hardware(int cpu)
{
	ipipe_timer_stop(cpu);
}

#ifdef CONFIG_XENO_OPT_VFILE

#include <cobalt/kernel/vfile.h>

static int timer_vfile_show(struct xnvfile_regular_iterator *it, void *data)
{
	const char *tm_status, *wd_status = "";

	tm_status = atomic_read(&nkclklk) > 0 ? "locked" : "on";
#ifdef CONFIG_XENO_OPT_WATCHDOG
	wd_status = "+watchdog";
#endif /* CONFIG_XENO_OPT_WATCHDOG */

	xnvfile_printf(it,
		       "status=%s%s:setup=%Lu:clock=%Lu:timerdev=%s:clockdev=%s\n",
		       tm_status, wd_status,
		       xnclock_ticks_to_ns(&nkclock, nktimerlat),
		       xnclock_read_raw(&nkclock),
		       ipipe_timer_name(), ipipe_clock_name());
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

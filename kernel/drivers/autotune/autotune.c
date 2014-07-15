/*
 * This file is part of the Xenomai project.
 *
 * Copyright (C) 2014 Philippe Gerum <rpm@xenomai.org>
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
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <cobalt/kernel/arith.h>
#include <rtdm/driver.h>
#include <rtdm/autotune.h>

#define AUTOTUNE_STEPS  60
#define ONE_SECOND	1000000000UL
#define H2G2_FACTOR(g)	((g) * 4 / 5)	/* 42 would be too pessimistic */

struct tuner_state {
	xnticks_t ideal;
	xnticks_t step;
	xnsticks_t min_lat;
	xnsticks_t max_lat;
	xnsticks_t sum_lat;
	unsigned long cur_samples;
	unsigned long max_samples;
};

struct gravity_tuner {
	const char *name;
	unsigned long (*get_gravity)(struct gravity_tuner *tuner);
	void (*set_gravity)(struct gravity_tuner *tuner, unsigned long gravity);
	void (*adjust_gravity)(struct gravity_tuner *tuner, long adjust);
	int (*init_tuner)(struct gravity_tuner *tuner);
	int (*start_tuner)(struct gravity_tuner *tuner, xnticks_t start_time,
			   xnticks_t interval);
	void (*destroy_tuner)(struct gravity_tuner *tuner);
	struct tuner_state state;
	rtdm_event_t done;
	int status;
};

struct irq_gravity_tuner {
	rtdm_timer_t timer;
	struct gravity_tuner tuner;
};

struct kthread_gravity_tuner {
	rtdm_task_t task;
	rtdm_event_t barrier;
	xnticks_t start_time;
	xnticks_t interval;
	struct gravity_tuner tuner;
};

struct uthread_gravity_tuner {
	rtdm_timer_t timer;
	rtdm_event_t pulse;
	struct gravity_tuner tuner;
};

struct autotune_context {
	struct gravity_tuner *tuner;
	int period;
};

static inline void init_tuner(struct gravity_tuner *tuner)
{
	rtdm_event_init(&tuner->done, 0);
	tuner->status = 0;
}

static inline void destroy_tuner(struct gravity_tuner *tuner)
{
	rtdm_event_destroy(&tuner->done);
}

static inline void done_sampling(struct gravity_tuner *tuner,
				 int status)
{
	tuner->status = status;
	rtdm_event_signal(&tuner->done);
}

static int add_sample(struct gravity_tuner *tuner, xnticks_t timestamp)
{
	struct tuner_state *state;
	xnsticks_t delta;

	state = &tuner->state;

	delta = (xnsticks_t)(timestamp - state->ideal);
	if (delta < state->min_lat)
		state->min_lat = delta;
	if (delta > state->max_lat)
		state->max_lat = delta;

	state->sum_lat += delta;
	state->ideal += state->step;

	if (++state->cur_samples >= state->max_samples) {
		done_sampling(tuner, 0);
		return 1;	/* Finished. */
	}

	return 0;	/* Keep going. */
}

static void timer_handler(rtdm_timer_t *timer)
{
	struct irq_gravity_tuner *irq_tuner;
	xnticks_t now;

	irq_tuner = container_of(timer, struct irq_gravity_tuner, timer);
	now = xnclock_read_raw(&nkclock);

	if (add_sample(&irq_tuner->tuner, now))
		rtdm_timer_stop_in_handler(timer);
}

static int init_irq_tuner(struct gravity_tuner *tuner)
{
	struct irq_gravity_tuner *irq_tuner;
	int ret;

	irq_tuner = container_of(tuner, struct irq_gravity_tuner, tuner);
	ret = rtdm_timer_init(&irq_tuner->timer, timer_handler, "autotune");
	if (ret)
		return ret;

	init_tuner(tuner);

	return 0;
}

static void destroy_irq_tuner(struct gravity_tuner *tuner)
{
	struct irq_gravity_tuner *irq_tuner;

	irq_tuner = container_of(tuner, struct irq_gravity_tuner, tuner);
	rtdm_timer_destroy(&irq_tuner->timer);
	destroy_tuner(tuner);
}

static unsigned long get_irq_gravity(struct gravity_tuner *tuner)
{
	return nkclock.gravity.irq;
}

static void set_irq_gravity(struct gravity_tuner *tuner, unsigned long gravity)
{
	nkclock.gravity.irq = gravity;
}

static void adjust_irq_gravity(struct gravity_tuner *tuner, long adjust)
{
	nkclock.gravity.irq += adjust;
}

static int start_irq_tuner(struct gravity_tuner *tuner,
			   xnticks_t start_time, xnticks_t interval)
{
	struct irq_gravity_tuner *irq_tuner;

	irq_tuner = container_of(tuner, struct irq_gravity_tuner, tuner);

	return rtdm_timer_start(&irq_tuner->timer, start_time,
				interval, RTDM_TIMERMODE_ABSOLUTE);
}

struct irq_gravity_tuner irq_tuner = {
	.tuner = {
		.name = "irqhand",
		.init_tuner = init_irq_tuner,
		.destroy_tuner = destroy_irq_tuner,
		.get_gravity = get_irq_gravity,
		.set_gravity = set_irq_gravity,
		.adjust_gravity = adjust_irq_gravity,
		.start_tuner = start_irq_tuner,
	},
};

void task_handler(void *arg)
{
	struct kthread_gravity_tuner *k_tuner = arg;
	xnticks_t now;
	int ret = 0;

	for (;;) {
		if (rtdm_task_should_stop())
			break;

		ret = rtdm_event_wait(&k_tuner->barrier);
		if (ret)
			break;

		ret = xnthread_set_periodic(&k_tuner->task, k_tuner->start_time,
					    XN_ABSOLUTE, k_tuner->interval);
		if (ret)
			break;

		for (;;) {
			ret = rtdm_task_wait_period();
			if (ret)
				break;

			now = xnclock_read_raw(&nkclock);
			if (add_sample(&k_tuner->tuner, now)) {
				rtdm_task_set_period(&k_tuner->task, 0);
				break;
			}
		}
	}

	done_sampling(&k_tuner->tuner, ret);
	rtdm_task_destroy(&k_tuner->task);
}

static int init_kthread_tuner(struct gravity_tuner *tuner)
{
	struct kthread_gravity_tuner *k_tuner;

	init_tuner(tuner);
	k_tuner = container_of(tuner, struct kthread_gravity_tuner, tuner);
	rtdm_event_init(&k_tuner->barrier, 0);

	return rtdm_task_init(&k_tuner->task, "autotune",
			      task_handler, k_tuner,
			      RTDM_TASK_HIGHEST_PRIORITY, 0);
}

static void destroy_kthread_tuner(struct gravity_tuner *tuner)
{
	struct kthread_gravity_tuner *k_tuner;

	k_tuner = container_of(tuner, struct kthread_gravity_tuner, tuner);
	rtdm_task_destroy(&k_tuner->task);
	rtdm_event_destroy(&k_tuner->barrier);
}

static unsigned long get_kthread_gravity(struct gravity_tuner *tuner)
{
	return nkclock.gravity.kernel;
}

static void set_kthread_gravity(struct gravity_tuner *tuner, unsigned long gravity)
{
	nkclock.gravity.kernel = gravity;
}

static void adjust_kthread_gravity(struct gravity_tuner *tuner, long adjust)
{
	nkclock.gravity.kernel += adjust;
}

static int start_kthread_tuner(struct gravity_tuner *tuner,
			       xnticks_t start_time, xnticks_t interval)
{
	struct kthread_gravity_tuner *k_tuner;

	k_tuner = container_of(tuner, struct kthread_gravity_tuner, tuner);

	k_tuner->start_time = start_time;
	k_tuner->interval = interval;
	rtdm_event_signal(&k_tuner->barrier);

	return 0;
}

struct kthread_gravity_tuner kthread_tuner = {
	.tuner = {
		.name = "kthread",
		.init_tuner = init_kthread_tuner,
		.destroy_tuner = destroy_kthread_tuner,
		.get_gravity = get_kthread_gravity,
		.set_gravity = set_kthread_gravity,
		.adjust_gravity = adjust_kthread_gravity,
		.start_tuner = start_kthread_tuner,
	},
};

static void pulse_handler(rtdm_timer_t *timer)
{
	struct uthread_gravity_tuner *u_tuner;

	u_tuner = container_of(timer, struct uthread_gravity_tuner, timer);
	rtdm_event_signal(&u_tuner->pulse);
}

static int init_uthread_tuner(struct gravity_tuner *tuner)
{
	struct uthread_gravity_tuner *u_tuner;
	int ret;

	u_tuner = container_of(tuner, struct uthread_gravity_tuner, tuner);
	ret = rtdm_timer_init(&u_tuner->timer, pulse_handler, "autotune");
	if (ret)
		return ret;

	xntimer_set_gravity(&u_tuner->timer, XNTIMER_UGRAVITY); /* gasp... */
	rtdm_event_init(&u_tuner->pulse, 0);
	init_tuner(tuner);

	return 0;
}

static void destroy_uthread_tuner(struct gravity_tuner *tuner)
{
	struct uthread_gravity_tuner *u_tuner;

	u_tuner = container_of(tuner, struct uthread_gravity_tuner, tuner);
	rtdm_timer_destroy(&u_tuner->timer);
	rtdm_event_destroy(&u_tuner->pulse);
}

static unsigned long get_uthread_gravity(struct gravity_tuner *tuner)
{
	return nkclock.gravity.user;
}

static void set_uthread_gravity(struct gravity_tuner *tuner, unsigned long gravity)
{
	nkclock.gravity.user = gravity;
}

static void adjust_uthread_gravity(struct gravity_tuner *tuner, long adjust)
{
	nkclock.gravity.user += adjust;
}

static int start_uthread_tuner(struct gravity_tuner *tuner,
			       xnticks_t start_time, xnticks_t interval)
{
	struct uthread_gravity_tuner *u_tuner;

	u_tuner = container_of(tuner, struct uthread_gravity_tuner, tuner);

	return rtdm_timer_start(&u_tuner->timer, start_time,
				interval, RTDM_TIMERMODE_ABSOLUTE);
}

static int add_uthread_sample(struct gravity_tuner *tuner,
			      nanosecs_abs_t user_timestamp)
{
	struct uthread_gravity_tuner *u_tuner;
	int ret;

	u_tuner = container_of(tuner, struct uthread_gravity_tuner, tuner);

	if (user_timestamp &&
	    add_sample(tuner, xnclock_ns_to_ticks(&nkclock, user_timestamp))) {
		rtdm_timer_stop(&u_tuner->timer);
		/* Tell the caller to park until next round. */
		ret = -EPIPE;
	} else
		ret = rtdm_event_wait(&u_tuner->pulse);

	return ret;
}

struct uthread_gravity_tuner uthread_tuner = {
	.tuner = {
		.name = "uthread",
		.init_tuner = init_uthread_tuner,
		.destroy_tuner = destroy_uthread_tuner,
		.get_gravity = get_uthread_gravity,
		.set_gravity = set_uthread_gravity,
		.adjust_gravity = adjust_uthread_gravity,
		.start_tuner = start_uthread_tuner,
	},
};

static int tune_gravity(struct gravity_tuner *tuner, int period)
{
	unsigned long old_gravity, gravity;
	struct tuner_state *state;
	int ret, step, wedge;
	xnsticks_t minlat;
	long adjust;

	state = &tuner->state;
	state->step = xnclock_ns_to_ticks(&nkclock, period);
	state->max_samples = ONE_SECOND / (period ?: 1);
	minlat = xnclock_ns_to_ticks(&nkclock, ONE_SECOND);
	old_gravity = tuner->get_gravity(tuner);
	tuner->set_gravity(tuner, 0);
	gravity = 0;
	wedge = 0;

	/*
	 * The tuning process is basic: we run a latency test for one
	 * second, increasing the clock gravity value by 2/3rd until
	 * we reach the wedge value or cause early shots, whichever
	 * comes first.
	 */
	for (step = 1; step <= AUTOTUNE_STEPS; step++) {
		state->ideal = xnclock_read_raw(&nkclock) + state->step * 3;
		state->min_lat = xnclock_ns_to_ticks(&nkclock, ONE_SECOND);
		state->max_lat = 0;
		state->sum_lat = 0;
		state->cur_samples = 0;

		ret = tuner->start_tuner(tuner,
					 xnclock_ticks_to_ns(&nkclock, state->ideal),
					 period);
		if (ret)
			goto fail;

		/* Tuner stops when posting. */
		ret = rtdm_event_wait(&tuner->done);
		if (ret)
			goto fail;

		ret = tuner->status;
		if (ret)
			goto fail;

		minlat = state->min_lat;
		if (minlat <= 0) {
			minlat = 0;
			goto done;
		}

		/*
		 * If we detect worse latencies with smaller gravity
		 * values across consecutive tests, we assume the
		 * former is our wedge value.  Retry and confirm it 5
		 * times before stopping.
		 */
		if (state->min_lat > minlat) {
#ifdef CONFIG_XENO_OPT_DEBUG_NUCLEUS
			printk(XENO_INFO "autotune[%s]: "
			       "at wedge (min_ns %Ld => %Ld), gravity reset to %Ld ns\n",
			       tuner->name,
			       xnclock_ticks_to_ns(&nkclock, minlat),
			       xnclock_ticks_to_ns(&nkclock, minlat),
			       xnclock_ticks_to_ns(&nkclock, gravity));
#endif
			if (++wedge >= 5)
				goto done;
			tuner->set_gravity(tuner, gravity);
			continue;
		}

		/*
		 * We seem to have a margin for compensating even
		 * more, increase the gravity value by a 3rd for next
		 * round.
		 */
		adjust = (long)xnarch_llimd(minlat, 2, 3);
		if (adjust == 0)
			goto done;

		gravity = tuner->get_gravity(tuner);
		tuner->adjust_gravity(tuner, adjust);
#ifdef CONFIG_XENO_OPT_DEBUG_NUCLEUS
		printk(XENO_INFO "autotune[%s]: min=%Ld | max=%Ld | avg=%Ld | gravity(%lut + adj=%ldt)\n",
		       tuner->name,
		       xnclock_ticks_to_ns(&nkclock, minlat),
		       xnclock_ticks_to_ns(&nkclock, state->max_lat),
		       xnclock_ticks_to_ns(&nkclock,
			   xnarch_llimd(state->sum_lat, 1, (state->cur_samples ?: 1))),
		       gravity, adjust);
#endif
	}

	printk(XENO_INFO "could not auto-tune (%s) after %ds\n",
	       tuner->name, AUTOTUNE_STEPS);

	return -EINVAL;
done:
	tuner->set_gravity(tuner, H2G2_FACTOR(gravity));

	printk(XENO_INFO "auto-tuning[%s]: gravity_ns=%Ld, min_ns=%Ld\n",
	       tuner->name,
	       xnclock_ticks_to_ns(&nkclock, tuner->get_gravity(tuner)),
	       step > 1 ? xnclock_ticks_to_ns(&nkclock, minlat) : 0);

	return 0;
fail:
	tuner->set_gravity(tuner, old_gravity);

	return ret;
}

static int autotune_ioctl_nrt(struct rtdm_fd *fd, unsigned int request, void *arg)
{
	struct autotune_context *context;
	struct gravity_tuner *tuner;
	int period, ret;

	context = rtdm_fd_to_private(fd);

	/* Clear previous tuner. */
	tuner = context->tuner;
	if (tuner) {
		tuner->destroy_tuner(tuner);
		context->tuner = NULL;
	}

	switch (request) {
	case AUTOTUNE_RTIOC_IRQ:
		tuner = &irq_tuner.tuner;
		break;
	case AUTOTUNE_RTIOC_KERN:
		tuner = &kthread_tuner.tuner;
		break;
	case AUTOTUNE_RTIOC_USER:
		tuner = &uthread_tuner.tuner;
		break;
	case AUTOTUNE_RTIOC_RESET:
		xnclock_reset_gravity(&nkclock);
		return 0;
	default:
		return -EINVAL;
	}

	ret = rtdm_safe_copy_from_user(fd, &period, arg, sizeof(period));
	if (ret)
		return ret;

	ret = tuner->init_tuner(tuner);
	if (ret)
		return ret;

	context->tuner = tuner;
	context->period = period;
	printk(XENO_INFO "auto-tuning core clock gravity, %s\n", tuner->name);

	return ret;
}

static int autotune_ioctl_rt(struct rtdm_fd *fd, unsigned int request, void *arg)
{
	struct autotune_context *context;
	struct gravity_tuner *tuner;
	nanosecs_abs_t timestamp;
	unsigned long gravity;
	int period, ret;

	context = rtdm_fd_to_private(fd);
	tuner = context->tuner;
	if (tuner == NULL)
		return -ENOSYS;

	period = context->period;

	switch (request) {
	case AUTOTUNE_RTIOC_RUN:
		ret = tune_gravity(tuner, period);
		if (ret)
			break;
		gravity = xnclock_ticks_to_ns(&nkclock,
					      tuner->get_gravity(tuner));
		ret = rtdm_safe_copy_to_user(fd, arg, &gravity,
					     sizeof(gravity));
		break;
	case AUTOTUNE_RTIOC_PULSE:
		if (tuner != &uthread_tuner.tuner)
			return -EINVAL;
		ret = rtdm_safe_copy_from_user(fd, &timestamp, arg,
					       sizeof(timestamp));
		if (ret)
			return ret;
		ret = add_uthread_sample(tuner, timestamp);
		break;
	default:
		ret = -ENOSYS;
	}

	return ret;
}

static int autotune_open(struct rtdm_fd *fd, int oflags)
{
	struct autotune_context *context;

	context = rtdm_fd_to_private(fd);
	context->tuner = NULL;

	return 0;
}

static void autotune_close(struct rtdm_fd *fd)
{
	struct autotune_context *context;
	struct gravity_tuner *tuner;

	context = rtdm_fd_to_private(fd);
	tuner = context->tuner;
	if (tuner)
		tuner->destroy_tuner(tuner);
}

static struct rtdm_device device = {
	.struct_version		=	RTDM_DEVICE_STRUCT_VER,
	.device_flags		=	RTDM_NAMED_DEVICE|RTDM_EXCLUSIVE,
	.context_size		=	sizeof(struct autotune_context),
	.open			=	autotune_open,
	.ops = {
		.ioctl_rt	=	autotune_ioctl_rt,
		.ioctl_nrt	=	autotune_ioctl_nrt,
		.close		=	autotune_close,
	},
	.device_class		=	RTDM_CLASS_AUTOTUNE,
	.device_sub_class	=	RTDM_SUBCLASS_AUTOTUNE,
	.device_name		=	"autotune",
	.driver_name		=	"autotune",
	.driver_version		=	RTDM_DRIVER_VER(1, 0, 0),
	.peripheral_name	=	"Auto-tuning services",
	.proc_name		=	device.device_name,
	.provider_name		=	"Philippe Gerum <rpm@xenomai.org>",
};

static int __init autotune_init(void)
{
	int ret;

	if (!realtime_core_enabled())
		return 0;

	ret = rtdm_dev_register(&device);
	if (ret)
		return ret;

	return 0;
}

device_initcall(autotune_init);

MODULE_LICENSE("GPL");

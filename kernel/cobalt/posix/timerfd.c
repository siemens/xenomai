/*
 * Copyright (C) 2013 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
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

#include <linux/timerfd.h>
#include <linux/err.h>
#include <cobalt/kernel/timer.h>
#include <cobalt/kernel/select.h>
#include <rtdm/fd.h>
#include "internal.h"
#include "clock.h"
#include "timer.h"
#include "timerfd.h"

struct cobalt_tfd {
	int flags;
	clockid_t clockid;
	struct rtdm_fd fd;
	struct xntimer timer;
	DECLARE_XNSELECT(read_select);
	struct itimerspec value;
	struct xnsynch readers;
	struct xnthread *target;
};

#define COBALT_TFD_TICKED	(1 << 2)

#define COBALT_TFD_SETTIME_FLAGS (TFD_TIMER_ABSTIME | TFD_WAKEUP)

static ssize_t timerfd_read(struct rtdm_fd *fd, void __user *buf, size_t size)
{
	unsigned long long __user *u_ticks;
	unsigned long long ticks = 0;
	struct cobalt_tfd *tfd;
	bool aligned;
	spl_t s;
	int err;

	if (size < sizeof(ticks))
		return -EINVAL;

	u_ticks = buf;
	aligned = (((unsigned long)buf) & (sizeof(ticks) - 1)) == 0;

	tfd = container_of(fd, struct cobalt_tfd, fd);

	xnlock_get_irqsave(&nklock, s);
	if (tfd->flags & COBALT_TFD_TICKED) {
		err = 0;
		goto out;
	}
	if (tfd->flags & TFD_NONBLOCK) {
		err = -EAGAIN;
		goto out;
	}

	do {
		err = xnsynch_sleep_on(&tfd->readers, XN_INFINITE, XN_RELATIVE);
	} while (err == 0 && (tfd->flags & COBALT_TFD_TICKED) == 0);

	if (err & XNBREAK)
		err = -EINTR;
  out:
	if (err == 0) {
		xnticks_t now;

		if (xntimer_periodic_p(&tfd->timer)) {
			now = xnclock_read_raw(xntimer_clock(&tfd->timer));
			ticks = 1 + xntimer_get_overruns(&tfd->timer, now);
		} else
			ticks = 1;

		tfd->flags &= ~COBALT_TFD_TICKED;
		xnselect_signal(&tfd->read_select, 0);
	}
	xnlock_put_irqrestore(&nklock, s);

	if (err == 0) {
		if (aligned)
			err = __xn_put_user(ticks, u_ticks);
		else
			err = __xn_copy_to_user(buf, &ticks, sizeof(ticks));
		if (err)
			err = -EFAULT;
	}

	return err ?: sizeof(ticks);
}

static int
timerfd_select_bind(struct rtdm_fd *fd, struct xnselector *selector,
		    unsigned type, unsigned index)
{
	struct cobalt_tfd *tfd = container_of(fd, struct cobalt_tfd, fd);
	struct xnselect_binding *binding;
	spl_t s;
	int err;

	if (type != XNSELECT_READ)
		return -EBADF;

	binding = xnmalloc(sizeof(*binding));
	if (binding == NULL)
		return -ENOMEM;

	xnlock_get_irqsave(&nklock, s);
	xntimer_set_sched(&tfd->timer, xnsched_current());
	err = xnselect_bind(&tfd->read_select, binding, selector, type,
			index, tfd->flags & COBALT_TFD_TICKED);
	xnlock_put_irqrestore(&nklock, s);

	return err;
}

static void timerfd_close(struct rtdm_fd *fd)
{
	struct cobalt_tfd *tfd = container_of(fd, struct cobalt_tfd, fd);
	int resched;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	xntimer_destroy(&tfd->timer);
	resched = xnsynch_destroy(&tfd->readers) == XNSYNCH_RESCHED;
	xnlock_put_irqrestore(&nklock, s);
	xnselect_destroy(&tfd->read_select);
	xnfree(tfd);

	if (resched)
		xnsched_run();
}

static struct rtdm_fd_ops timerfd_ops = {
	.read_rt = timerfd_read,
	.select_bind = timerfd_select_bind,
	.close = timerfd_close,
};

static void timerfd_handler(struct xntimer *xntimer)
{
	struct cobalt_tfd *tfd;

	tfd = container_of(xntimer, struct cobalt_tfd, timer);
	tfd->flags |= COBALT_TFD_TICKED;
	xnselect_signal(&tfd->read_select, 1);
	xnsynch_wakeup_one_sleeper(&tfd->readers);
	if (tfd->target)
		xnthread_unblock(tfd->target);
}

int cobalt_timerfd_create(int ufd, int clockid, int flags)
{
	struct cobalt_tfd *tfd;
	struct xnthread *curr;
	struct xnsys_ppd *p;

	p = xnsys_ppd_get(0);
	if (p == &__xnsys_global_ppd)
		return -EPERM;

	if (clockid != CLOCK_REALTIME && clockid != CLOCK_MONOTONIC)
		return -EINVAL;

	if (flags & ~TFD_CREATE_FLAGS)
		return -EINVAL;

	tfd = xnmalloc(sizeof(*tfd));
	if (tfd == NULL)
		return -ENOMEM;

	tfd->flags = flags;
	tfd->clockid = clockid;
	curr = xnshadow_current();
	xntimer_init(&tfd->timer, &nkclock, timerfd_handler,
		     curr ? xnthread_sched(curr) : NULL, XNTIMER_UGRAVITY);
	xnsynch_init(&tfd->readers, XNSYNCH_PRIO | XNSYNCH_NOPIP, NULL);
	xnselect_init(&tfd->read_select);
	tfd->target = NULL;

	return rtdm_fd_enter(p, &tfd->fd, ufd, COBALT_TIMERFD_MAGIC,
			&timerfd_ops);
}

static inline struct cobalt_tfd *tfd_get(int ufd)
{
	struct rtdm_fd *fd;

	fd = rtdm_fd_get(xnsys_ppd_get(0), ufd, COBALT_TIMERFD_MAGIC);
	if (IS_ERR(fd)) {
		int err = PTR_ERR(fd);
		if (err == -EBADF && cobalt_process_context() == NULL)
			err = -EPERM;
		return ERR_PTR(err);
	}

	return container_of(fd, struct cobalt_tfd, fd);
}

static inline void tfd_put(struct cobalt_tfd *tfd)
{
	rtdm_fd_put(&tfd->fd);
}

int cobalt_timerfd_settime(int fd, int flags,
			const struct itimerspec __user *new_value,
			struct itimerspec __user *old_value)
{
	struct itimerspec ovalue, value;
	struct cobalt_tfd *tfd;
	int cflag;
	int err;
	spl_t s;

	if (flags & ~COBALT_TFD_SETTIME_FLAGS)
		return -EINVAL;

	tfd = tfd_get(fd);
	if (IS_ERR(tfd))
		return PTR_ERR(tfd);

	if (!new_value ||
		__xn_copy_from_user(&value, new_value, sizeof(value))) {
		err = -EFAULT;
		goto out;
	}

	cflag = (flags & TFD_TIMER_ABSTIME) ? TIMER_ABSTIME : 0;

	xnlock_get_irqsave(&nklock, s);

	if (flags & TFD_WAKEUP) {
		tfd->target = xnshadow_current();
		if (tfd->target == NULL) {
			err = -EPERM;
			goto out_unlock;
		}
	} else
		tfd->target = NULL;

	if (old_value)
		cobalt_xntimer_gettime(&tfd->timer, &ovalue);

	xntimer_set_sched(&tfd->timer, xnsched_current());

	err = cobalt_xntimer_settime(&tfd->timer,
				clock_flag(cflag, tfd->clockid), &value);
  out_unlock:
	xnlock_put_irqrestore(&nklock, s);

	if (err == 0 && old_value &&
		__xn_copy_to_user(old_value, &ovalue, sizeof(ovalue))) {
		xnlock_get_irqsave(&nklock, s);
		xntimer_stop(&tfd->timer);
		tfd->target = NULL;
		xnlock_put_irqrestore(&nklock, s);

		err = -EFAULT;
	}

  out:
	tfd_put(tfd);

	return err;
}

int cobalt_timerfd_gettime(int fd, struct itimerspec __user *curr_value)
{
	struct itimerspec value;
	struct cobalt_tfd *tfd;
	int err = 0;
	spl_t s;

	tfd = tfd_get(fd);
	if (IS_ERR(tfd))
		return PTR_ERR(tfd);

	xnlock_get_irqsave(&nklock, s);
	cobalt_xntimer_gettime(&tfd->timer, &value);
	xnlock_put_irqrestore(&nklock, s);

	if (!curr_value || __xn_copy_to_user(curr_value, &value, sizeof(value)))
		err = -EFAULT;

	tfd_put(tfd);

	return err;
}

/**
 * @file
 * @note Copyright (C) 2006-2011 Philippe Gerum <rpm@xenomai.org>.
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
 * \ingroup clock
 */

/*!
 * \ingroup nucleus
 * \defgroup clock System clock services.
 *
 *@{*/

#include <nucleus/pod.h>
#include <nucleus/timer.h>
#include <nucleus/module.h>

/*!
 * \fn void xnclock_adjust(xnsticks_t delta)
 * \brief Adjust the clock time for the system.
 *
 * Xenomai tracks the current time as a monotonously increasing count
 * of ticks since the epoch. The epoch is initially the same as the
 * underlying machine time.
 *
 * This service changes the epoch for the system by applying the
 * specified tick delta on the wallclock offset.
 *
 * @param delta The adjustment of the system time expressed in ticks.
 *
 * @note This routine must be entered nklock locked, interrupts off.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Any kernel context.
 *
 * Rescheduling: never.
 */

void xnclock_adjust(xnsticks_t delta)
{
	xnticks_t now;

	nkclock.wallclock_offset += delta;
	now = xnarch_get_cpu_time() + nkclock.wallclock_offset;
	xntimer_adjust_all(delta);

	trace_mark(xn_nucleus, clock_adjust, "delta %Lu", delta);
}
EXPORT_SYMBOL_GPL(xnclock_adjust);

#ifdef CONFIG_XENO_OPT_VFILE

#ifdef CONFIG_XENO_OPT_STATS

static struct xnvfile_snapshot_ops tmstat_vfile_ops;

struct tmstat_vfile_priv {
	struct xnholder *curr;
};

struct tmstat_vfile_data {
	int cpu;
	unsigned int scheduled;
	unsigned int fired;
	xnticks_t timeout;
	xnticks_t interval;
	xnflags_t status;
	char handler[12];
	char name[XNOBJECT_NAME_LEN];
};

static int tmstat_vfile_rewind(struct xnvfile_snapshot_iterator *it)
{
	struct tmstat_vfile_priv *priv = xnvfile_iterator_priv(it);

	priv->curr = getheadq(&nkclock.timerq);

	return countq(&nkclock.timerq);
}

static int tmstat_vfile_next(struct xnvfile_snapshot_iterator *it, void *data)
{
	struct tmstat_vfile_priv *priv = xnvfile_iterator_priv(it);
	struct tmstat_vfile_data *p = data;
	struct xntimer *timer;

	if (priv->curr == NULL)
		return 0;

	timer = tblink2timer(priv->curr);
	priv->curr = nextq(&nkclock.timerq, priv->curr);

	if (xnstat_counter_get(&timer->scheduled) == 0)
		return VFILE_SEQ_SKIP;

	p->cpu = xnsched_cpu(xntimer_sched(timer));
	p->scheduled = xnstat_counter_get(&timer->scheduled);
	p->fired = xnstat_counter_get(&timer->fired);
	p->timeout = xntimer_get_timeout(timer);
	p->interval = xntimer_get_interval(timer);
	p->status = timer->status;
	memcpy(p->handler, timer->handler_name,
	       sizeof(p->handler)-1);
	p->handler[sizeof(p->handler)-1] = 0;
	xnobject_copy_name(p->name, timer->name);

	return 1;
}

static int tmstat_vfile_show(struct xnvfile_snapshot_iterator *it, void *data)
{
	struct tmstat_vfile_data *p = data;
	char timeout_buf[]  = "-         ";
	char interval_buf[] = "-         ";

	if (p == NULL)
		xnvfile_printf(it,
			       "%-3s  %-10s  %-10s  %-10s  %-10s  %-11s  %-15s\n",
			       "CPU", "SCHEDULED", "FIRED", "TIMEOUT",
			       "INTERVAL", "HANDLER", "NAME");
	else {
		if (!testbits(p->status, XNTIMER_DEQUEUED))
			snprintf(timeout_buf, sizeof(timeout_buf), "%-10llu",
				 p->timeout);
		if (testbits(p->status, XNTIMER_PERIODIC))
			snprintf(interval_buf, sizeof(interval_buf), "%-10llu",
				 p->interval);
		xnvfile_printf(it,
			       "%-3u  %-10u  %-10u  %s  %s  %-11s  %-15s\n",
			       p->cpu, p->scheduled, p->fired, timeout_buf,
			       interval_buf, p->handler, p->name);
	}

	return 0;
}

static struct xnvfile_snapshot_ops tmstat_vfile_ops = {
	.rewind = tmstat_vfile_rewind,
	.next = tmstat_vfile_next,
	.show = tmstat_vfile_show,
};

void xnclock_init_proc(void)
{
	struct xnclock *p = &nkclock;

	memset(&p->vfile, 0, sizeof(p->vfile));
	p->vfile.privsz = sizeof(struct tmstat_vfile_priv);
	p->vfile.datasz = sizeof(struct tmstat_vfile_data);
	p->vfile.tag = &p->revtag;
	p->vfile.ops = &tmstat_vfile_ops;

	xnvfile_init_snapshot("timerstat", &p->vfile, &nkvfroot);
	xnvfile_priv(&p->vfile) = p;
}

void xnclock_cleanup_proc(void)
{
	xnvfile_destroy_snapshot(&nkclock.vfile);
}

#endif /* CONFIG_XENO_OPT_STATS */

#endif /* CONFIG_XENO_OPT_VFILE */

struct xnclock nkclock = {
#ifdef CONFIG_XENO_OPT_STATS
	.timerq = XNQUEUE_INITIALIZER(nkclock.timerq),
#endif /* CONFIG_XENO_OPT_STATS */
};
EXPORT_SYMBOL_GPL(nkclock);

/*@}*/
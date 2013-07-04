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

#include <cobalt/kernel/pod.h>
#include <cobalt/kernel/timer.h>
#include <cobalt/kernel/clock.h>
#include <cobalt/kernel/arith.h>

static unsigned long long clockfreq;

#ifdef XNARCH_HAVE_LLMULSHFT

static unsigned int tsc_scale, tsc_shift;

#ifdef XNARCH_HAVE_NODIV_LLIMD

static struct xnarch_u32frac tsc_frac;
static struct xnarch_u32frac bln_frac;

long long xnclock_ns_to_ticks(long long ns)
{
	return xnarch_nodiv_llimd(ns, tsc_frac.frac, tsc_frac.integ);
}

unsigned long long xnclock_divrem_billion(unsigned long long value,
					  unsigned long *rem)
{
	unsigned long long q;
	unsigned r;

	q = xnarch_nodiv_ullimd(value, bln_frac.frac, bln_frac.integ);
	r = value - q * 1000000000;
	if (r >= 1000000000) {
		++q;
		r -= 1000000000;
	}
	*rem = r;
	return q;
}

#else /* !XNARCH_HAVE_NODIV_LLIMD */

long long xnclock_ns_to_ticks(long long ns)
{
	return xnarch_llimd(ns, 1 << tsc_shift, tsc_scale);
}

#endif /* !XNARCH_HAVE_NODIV_LLIMD */

xnsticks_t xnclock_ticks_to_ns(xnsticks_t ticks)
{
	return xnarch_llmulshft(ticks, tsc_scale, tsc_shift);
}

xnsticks_t xnclock_ticks_to_ns_rounded(xnsticks_t ticks)
{
	unsigned int shift = tsc_shift - 1;
	return (xnarch_llmulshft(ticks, tsc_scale, shift) + 1) / 2;
}

#else  /* !XNARCH_HAVE_LLMULSHFT */

xnsticks_t xnclock_ticks_to_ns(xnsticks_t ticks)
{
	return xnarch_llimd(ticks, 1000000000, clockfreq);
}

xnsticks_t xnclock_ticks_to_ns_rounded(xnsticks_t ticks)
{
	return (xnarch_llimd(ticks, 1000000000, clockfreq/2) + 1) / 2;
}

xnsticks_t xnclock_ns_to_ticks(xnsticks_t ns)
{
	return xnarch_llimd(ns, clockfreq, 1000000000);
}
#endif /* !XNARCH_HAVE_LLMULSHFT */

#ifndef XNARCH_HAVE_NODIV_LLIMD
unsigned long long xnclock_divrem_billion(unsigned long long value,
					  unsigned long *rem)
{
	return xnarch_ulldiv(value, 1000000000, rem);

}
#endif /* !XNARCH_HAVE_NODIV_LLIMD */

EXPORT_SYMBOL_GPL(xnclock_ticks_to_ns);
EXPORT_SYMBOL_GPL(xnclock_ticks_to_ns_rounded);
EXPORT_SYMBOL_GPL(xnclock_ns_to_ticks);
EXPORT_SYMBOL_GPL(xnclock_divrem_billion);

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
	now = xnclock_read_monotonic() + nkclock.wallclock_offset;
	xntimer_adjust_all(delta);

	trace_mark(xn_nucleus, clock_adjust, "delta %Lu", delta);
}
EXPORT_SYMBOL_GPL(xnclock_adjust);

xnticks_t xnclock_get_host_time(void)
{
	struct timeval tv;
	do_gettimeofday(&tv);
	return tv.tv_sec * 1000000000ULL + tv.tv_usec * 1000;
}
EXPORT_SYMBOL_GPL(xnclock_get_host_time);

xnticks_t xnclock_read_monotonic(void)
{
	return xnclock_ticks_to_ns(xnclock_read_raw());
}
EXPORT_SYMBOL_GPL(xnclock_read_monotonic);

#ifdef CONFIG_XENO_OPT_VFILE

#ifdef CONFIG_XENO_OPT_STATS

static struct xnvfile_snapshot_ops tmstat_vfile_ops;

struct tmstat_vfile_priv {
	struct xntimer *curr;
};

struct tmstat_vfile_data {
	int cpu;
	unsigned int scheduled;
	unsigned int fired;
	xnticks_t timeout;
	xnticks_t interval;
	unsigned long status;
	char handler[12];
	char name[XNOBJECT_NAME_LEN];
};

static int tmstat_vfile_rewind(struct xnvfile_snapshot_iterator *it)
{
	struct tmstat_vfile_priv *priv = xnvfile_iterator_priv(it);

	if (list_empty(&nkclock.timerq)) {
		priv->curr = NULL;
		return 0;
	}

	priv->curr = list_first_entry(&nkclock.timerq, struct xntimer, tblink);

	return nkclock.nrtimers;
}

static int tmstat_vfile_next(struct xnvfile_snapshot_iterator *it, void *data)
{
	struct tmstat_vfile_priv *priv = xnvfile_iterator_priv(it);
	struct tmstat_vfile_data *p = data;
	struct xntimer *timer;

	if (priv->curr == NULL)
		return 0;

	timer = priv->curr;
	if (list_is_last(&timer->tblink, &nkclock.timerq))
		priv->curr = NULL;
	else
		priv->curr = list_entry(timer->tblink.next,
					struct xntimer, tblink);

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
		if ((p->status & XNTIMER_DEQUEUED) == 0)
			snprintf(timeout_buf, sizeof(timeout_buf), "%-10llu",
				 p->timeout);
		if (p->status & XNTIMER_PERIODIC)
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
	.timerq = LIST_HEAD_INIT(nkclock.timerq),
#endif /* CONFIG_XENO_OPT_STATS */
};
EXPORT_SYMBOL_GPL(nkclock);

void __init xnclock_init(unsigned long long freq)
{
	clockfreq = freq;
#ifdef XNARCH_HAVE_LLMULSHFT
	xnarch_init_llmulshft(1000000000, freq, &tsc_scale, &tsc_shift);
#ifdef XNARCH_HAVE_NODIV_LLIMD
	xnarch_init_u32frac(&tsc_frac, 1 << tsc_shift, tsc_scale);
	xnarch_init_u32frac(&bln_frac, 1, 1000000000);
#endif
#endif
}

/*@}*/

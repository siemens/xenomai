/*
 * Copyright (C) 2004 Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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
 */

#ifndef _XENO_NUCLEUS_LTT_H
#define _XENO_NUCLEUS_LTT_H

#include <xenomai/nucleus/types.h>

#ifdef CONFIG_LTT

#include <linux/ltt-core.h>

struct xnltt_evmap {

    char *ltt_label;	/* !< Event label (creation time). */
    char *ltt_format;	/* !< Event format (creation time). */
    int ltt_evid;	/* !< LTT custom event id. */
    int ltt_filter;	/* !< Event filter. */
};

#define xeno_ev_ienter       0
#define xeno_ev_iexit        1
#define xeno_ev_resched      2
#define xeno_ev_smpsched     3
#define xeno_ev_fastsched    4
#define xeno_ev_switch       5
#define xeno_ev_fault        6
#define xeno_ev_callout      7
#define xeno_ev_finalize     8
#define xeno_ev_thrinit      9
#define xeno_ev_thrstart     10
#define xeno_ev_threstart    11
#define xeno_ev_thrdelete    12
#define xeno_ev_thrsuspend   13
#define xeno_ev_thresume     14
#define xeno_ev_thrunblock   15
#define xeno_ev_threnice     16
#define xeno_ev_cpumigrate   17
#define xeno_ev_sigdispatch  18
#define xeno_ev_thrboot      19
#define xeno_ev_tmtick       20
#define xeno_ev_sleepon      21
#define xeno_ev_wakeup1      22
#define xeno_ev_wakeupx      23
#define xeno_ev_syncflush    24
#define xeno_ev_syncforget   25
#define xeno_ev_lohandler    26
#define xeno_ev_primarysw    27
#define xeno_ev_primary      28
#define xeno_ev_secondarysw  29
#define xeno_ev_secondary    30
#define xeno_ev_shadowmap    31
#define xeno_ev_shadowunmap  32
#define xeno_ev_shadowstart  33
#define xeno_ev_syscall      34
#define xeno_ev_shadowexit   35
#define xeno_ev_thrsetmode   36
#define xeno_ev_rdrotate     37
#define xeno_ev_rractivate   38
#define xeno_ev_rrdeactivate 39
#define xeno_ev_timeset      40
#define xeno_ev_addhook      41
#define xeno_ev_remhook      42
#define xeno_ev_thrperiodic  43
#define xeno_ev_thrwait      44
#define xeno_ev_tmstart      45
#define xeno_ev_tmstop       46
#define xeno_ev_mark         47
#define xeno_ev_watchdog     48

#define xeno_evthr  0x1
#define xeno_evirq  0x2
#define xeno_evsys  0x4
#define xeno_evall  0x7

#define XNLTT_MAX_EVENTS 64

extern struct xnltt_evmap xnltt_evtable[];

extern int xnltt_filter;

#define xnltt_log_event(ev, args...) \
do { \
  if (xnltt_evtable[ev].ltt_filter & xnltt_filter) \
    ltt_log_std_formatted_event(xnltt_evtable[ev].ltt_evid, ##args); \
} while(0)

static inline void xnltt_set_filter (int mask)
{
    xnltt_filter = mask;
}

static inline void xnltt_stop_tracing (void)
{
    xnltt_set_filter(0);
}

void xnltt_log_mark(const char *fmt,
		    ...);

int xnltt_mount(void);

void xnltt_umount(void);

#else /* !CONFIG_LTT */

#define xnltt_log_event(ev, args...); /* Eat the semi-colon. */

static inline void xnltt_log_mark (const char *fmt, ...)
{
}

static inline void xnltt_set_filter (int mask)
{
}

static inline void xnltt_stop_tracing (void)
{
}

#endif /* CONFIG_LTT */

#endif /* !_XENO_NUCLEUS_LTT_H_ */

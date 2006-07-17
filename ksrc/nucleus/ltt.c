/*
 * Copyright (C) 2004 Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA
 * 02139, USA; either version 2 of the License, or (at your option)
 * any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/module.h>
#include <stdarg.h>
#include <nucleus/ltt.h>

void xnltt_log_mark(const char *fmt, ...)
{
	char markbuf[64];	/* Don't eat too much stack space. */
	va_list ap;

	if (xnltt_evtable[xeno_ev_mark].ltt_filter & xnltt_filter) {
		va_start(ap, fmt);
		vsnprintf(markbuf, sizeof(markbuf), fmt, ap);
		va_end(ap);
		ltt_log_std_formatted_event(xnltt_evtable[xeno_ev_mark].
					    ltt_evid, markbuf);
	}
}

int __init xnltt_mount(void)
{
	int ev, evid;

	/* Create all custom LTT events we need. */

	for (ev = 0; xnltt_evtable[ev].ltt_label != NULL; ev++) {
		evid = ltt_create_event(xnltt_evtable[ev].ltt_label,
					xnltt_evtable[ev].ltt_format,
					LTT_CUSTOM_EV_FORMAT_TYPE_STR, NULL);
		if (evid < 0) {
			while (--ev >= 0) {
				xnltt_evtable[ev].ltt_evid = -1;
				ltt_destroy_event(xnltt_evtable[ev].ltt_evid);
			}

			return evid;
		}

		xnltt_evtable[ev].ltt_evid = evid;
	}

#ifdef CONFIG_XENO_OPT_FILTER_EVALL
	xnltt_filter = ~xeno_evall;
#else /* !CONFIG_XENO_OPT_FILTER_EVALL */
#ifdef CONFIG_XENO_OPT_FILTER_EVIRQ
	xnltt_filter &= ~xeno_evirq;
#endif /* CONFIG_XENO_OPT_FILTER_EVIRQ */
#ifdef CONFIG_XENO_OPT_FILTER_EVTHR
	xnltt_filter &= ~xeno_evthr;
#endif /* CONFIG_XENO_OPT_FILTER_EVTHR */
#ifdef CONFIG_XENO_OPT_FILTER_EVSYS
	xnltt_filter &= ~xeno_evthr;
#endif /* CONFIG_XENO_OPT_FILTER_EVSYS */
#endif /* CONFIG_XENO_OPT_FILTER_EVALL */

	return 0;
}

void __exit xnltt_umount(void)
{
	int ev;

	for (ev = 0; xnltt_evtable[ev].ltt_evid != -1; ev++)
		ltt_destroy_event(xnltt_evtable[ev].ltt_evid);
}

struct xnltt_evmap xnltt_evtable[] = {

	[xeno_ev_ienter] = {"Xenomai i-enter", "irq=%d", -1, xeno_evirq},
	[xeno_ev_iexit] = {"Xenomai i-exit", "irq=%d", -1, xeno_evirq},
	[xeno_ev_resched] = {"Xenomai resched", NULL, -1, xeno_evthr},
	[xeno_ev_smpsched] = {"Xenomai smpsched", NULL, -1, xeno_evthr},
	[xeno_ev_fastsched] = {"Xenomai fastsched", NULL, -1, xeno_evthr},
	[xeno_ev_switch] = {"Xenomai ctxsw", "%s -> %s", -1, xeno_evthr},
	[xeno_ev_fault] =
	    {"Xenomai fault", "thread=%s, location=%p, trap=%d", -1,
	     xeno_evall},
	[xeno_ev_callout] =
	    {"Xenomai callout", "type=%s, thread=%s", -1, xeno_evall},
	[xeno_ev_finalize] = {"Xenomai finalize", "%s -> %s", -1, xeno_evall},
	[xeno_ev_thrinit] =
	    {"Xenomai thread init", "thread=%s, flags=0x%x", -1, xeno_evthr},
	[xeno_ev_thrstart] =
	    {"Xenomai thread start", "thread=%s", -1, xeno_evthr},
	[xeno_ev_threstart] =
	    {"Xenomai thread restart", "thread=%s", -1, xeno_evthr},
	[xeno_ev_thrdelete] =
	    {"Xenomai thread delete", "thread=%s", -1, xeno_evthr},
	[xeno_ev_thrsuspend] = {"Xenomai thread suspend",
				"thread=%s, mask=0x%x, timeout=%Lu, wchan=%p",
				-1, xeno_evthr},
	[xeno_ev_thresume] =
	    {"Xenomai thread resume", "thread=%s, mask=0x%x", -1, xeno_evthr},
	[xeno_ev_thrunblock] =
	    {"Xenomai thread unblock", "thread=%s, status=0x%x", -1,
	     xeno_evthr},
	[xeno_ev_threnice] =
	    {"Xenomai thread renice", "thread=%s, prio=%d", -1, xeno_evthr},
	[xeno_ev_cpumigrate] =
	    {"Xenomai CPU migrate", "thread=%s, cpu=%d", -1, xeno_evthr},
	[xeno_ev_sigdispatch] =
	    {"Xenomai sigdispatch", "thread=%s, sigpend=0x%x", -1, xeno_evall},
	[xeno_ev_thrboot] =
	    {"Xenomai thread begin", "thread=%s", -1, xeno_evthr},
	[xeno_ev_tmtick] =
	    {"Xenomai timer tick", "runthread=%s", -1, xeno_evirq},
	[xeno_ev_sleepon] =
	    {"Xenomai sleepon", "thread=%s, sync=%p", -1, xeno_evthr},
	[xeno_ev_wakeup1] =
	    {"Xenomai wakeup1", "thread=%s, sync=%p", -1, xeno_evthr},
	[xeno_ev_wakeupx] =
	    {"Xenomai wakeupx", "thread=%s, sync=%p", -1, xeno_evthr},
	[xeno_ev_syncflush] =
	    {"Xenomai syncflush", "sync=%p, reason=0x%x", -1, xeno_evthr},
	[xeno_ev_syncforget] =
	    {"Xenomai syncforget", "thread=%s, sync=%p", -1, xeno_evthr},
	[xeno_ev_lohandler] =
	    {"Xenomai lohandler", "type=%d, task=%s, pid=%d", -1, xeno_evall},
	[xeno_ev_primarysw] = {"Xenomai modsw1", "thread=%s", -1, xeno_evthr},
	[xeno_ev_primary] = {"Xenomai modex1", "thread=%s", -1, xeno_evthr},
	[xeno_ev_secondarysw] = {"Xenomai modsw2", "thread=%s", -1, xeno_evthr},
	[xeno_ev_secondary] = {"Xenomai modex2", "thread=%s", -1, xeno_evthr},
	[xeno_ev_shadowmap] =
	    {"Xenomai shadow map", "thread=%s, pid=%d, prio=%d", -1,
	     xeno_evthr},
	[xeno_ev_shadowunmap] =
	    {"Xenomai shadow unmap", "thread=%s, pid=%d", -1, xeno_evthr},
	[xeno_ev_shadowstart] =
	    {"Xenomai shadow start", "thread=%s", -1, xeno_evthr},
	[xeno_ev_syscall] =
	    {"Xenomai syscall", "thread=%s, skin=%d, call=%d", -1, xeno_evsys},
	[xeno_ev_shadowexit] =
	    {"Xenomai shadow exit", "thread=%s", -1, xeno_evthr},
	[xeno_ev_thrsetmode] =
	    {"Xenomai thread setmode", "thread=%s, clrmask=0x%x, setmask=0x%x",
	     -1,
	     xeno_evthr},
	[xeno_ev_rdrotate] =
	    {"Xenomai rotate readyq", "thread=%s, prio=%d", -1, xeno_evthr},
	[xeno_ev_rractivate] = {"Xenomai RR on", "quantum=%Lu", -1, xeno_evthr},
	[xeno_ev_rrdeactivate] = {"Xenomai RR off", NULL, -1, xeno_evthr},
	[xeno_ev_timeset] = {"Xenomai set time", "newtime=%Lu", -1, xeno_evall},
	[xeno_ev_addhook] =
	    {"Xenomai add hook", "type=%d, routine=%p", -1, xeno_evall},
	[xeno_ev_remhook] =
	    {"Xenomai remove hook", "type=%d, routine=%p", -1, xeno_evall},
	[xeno_ev_thrperiodic] =
	    {"Xenomai thread speriod", "thread=%s, idate=%Lu, period=%Lu", -1,
	     xeno_evthr},
	[xeno_ev_thrwait] =
	    {"Xenomai thread wperiod", "thread=%s", -1, xeno_evthr},
	[xeno_ev_tmstart] =
	    {"Xenomai start timer", "tick=%u ns", -1, xeno_evall},
	[xeno_ev_tmstop] = {"Xenomai stop timer", NULL, -1, xeno_evall},
	[xeno_ev_mark] = {"Xenomai **mark**", "%s", -1, xeno_evall},
	[xeno_ev_watchdog] =
	    {"Xenomai watchdog", "runthread=%s", -1, xeno_evall},
	{NULL, NULL, -1, 0},
};

int xnltt_filter = xeno_evall;

EXPORT_SYMBOL(xnltt_evtable);
EXPORT_SYMBOL(xnltt_filter);
EXPORT_SYMBOL(xnltt_log_mark);

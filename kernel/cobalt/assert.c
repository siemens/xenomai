/*
 * Copyright (C) 2013 Philippe Gerum <rpm@xenomai.org>.
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
#include <stdarg.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <cobalt/kernel/sched.h>
#include <cobalt/kernel/assert.h>
#include <cobalt/kernel/clock.h>

void (*nkpanic)(const char *format, ...) = panic;
EXPORT_SYMBOL_GPL(nkpanic);

void __xnsys_fatal(const char *format, ...)
{
	static char msg_buf[1024];
	struct xnthread *thread;
	struct xnsched *sched;
	static int oopsed;
	char pbuf[16];
	xnticks_t now;
	unsigned cpu;
	va_list ap;
	int cprio;
	spl_t s;

	xntrace_panic_freeze();
	ipipe_prepare_panic();

	xnlock_get_irqsave(&nklock, s);

	if (oopsed)
		goto out;

	oopsed = 1;
	va_start(ap, format);
	vsnprintf(msg_buf, sizeof(msg_buf), format, ap);
	printk(XENO_ERR "%s", msg_buf);
	va_end(ap);

	now = xnclock_read_monotonic(&nkclock);

	printk(KERN_ERR "\n %-3s  %-6s %-8s %-8s %-8s  %s\n",
	       "CPU", "PID", "PRI", "TIMEOUT", "STAT", "NAME");

	/*
	 * NOTE: &nkthreadq can't be empty, we have the root thread(s)
	 * linked there at least.
	 */
	for_each_online_cpu(cpu) {
		sched = xnsched_struct(cpu);
		list_for_each_entry(thread, &nkthreadq, glink) {
			if (thread->sched != sched)
				continue;
			cprio = xnthread_current_priority(thread);
			snprintf(pbuf, sizeof(pbuf), "%3d", cprio);
			printk(KERN_ERR "%c%3u  %-6d %-8s %-8Lu %.8lx  %s\n",
			       thread == sched->curr ? '>' : ' ',
			       cpu,
			       xnthread_host_pid(thread),
			       pbuf,
			       xnthread_get_timeout(thread, now),
			       xnthread_state_flags(thread),
			       xnthread_name(thread));
		}
	}

	printk(KERN_ERR "Master time base: clock=%Lu\n",
	       xnclock_read_raw(&nkclock));
#ifdef CONFIG_SMP
	printk(KERN_ERR "Current CPU: #%d\n", ipipe_processor_id());
#endif
out:
	xnlock_put_irqrestore(&nklock, s);

	show_stack(NULL,NULL);
	xntrace_panic_dump();
	for (;;)
		cpu_relax();
}
EXPORT_SYMBOL_GPL(__xnsys_fatal);

void __xnsys_assert_failed(const char *file, int line, const char *msg)
{
  	xntrace_panic_freeze();
	printk(XENO_ERR "assertion failed at %s:%d (%s)\n",
	       file, line, msg);
	xntrace_panic_dump();
}
EXPORT_SYMBOL_GPL(__xnsys_assert_failed);

/*
 * Copyright (C) 2011 Philippe Gerum <rpm@xenomai.org>.
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
 */

#ifndef _XENO_ASM_SH_BITS_INIT_H
#define _XENO_ASM_SH_BITS_INIT_H

#ifndef __KERNEL__
#error "Pure kernel header included from user-space!"
#endif

#include <linux/init.h>
#include <asm/xenomai/calibration.h>
#include <asm-generic/xenomai/bits/timeconv.h>

int xnarch_escalation_virq;

void xnpod_schedule_handler(void);

unsigned long xnarch_calibrate_timer(void)
{
	/*
	 * Compute the time needed to program the dedicated hrtimer.
	 * The return value is expressed in hrclock counter unit.
	 */
	return xnarch_ns_to_tsc(rthal_timer_calibrate());
}

int xnarch_calibrate_sched(void)
{
	nktimerlat = xnarch_calibrate_timer();
	if (nktimerlat == 0)
		return -ENODEV;

	nklatency = xnarch_ns_to_tsc(xnarch_get_sched_latency()) + nktimerlat;

	return 0;
}

static inline int xnarch_init(void)
{
	int ret;

	ret = rthal_init();
	if (ret)
		return ret;

	xnarch_init_timeconv(RTHAL_CLOCK_FREQ);

	ret = xnarch_calibrate_sched();
	if (ret)
		return ret;

	xnarch_escalation_virq = ipipe_alloc_virq();
	if (xnarch_escalation_virq == 0)
		return -ENOSYS;

	ipipe_request_irq(&rthal_archdata.domain,
			  xnarch_escalation_virq,
			  (ipipe_irq_handler_t)xnpod_schedule_handler,
			  NULL, NULL);
	return 0;
}

static inline void xnarch_exit(void)
{
	ipipe_free_virq(xnarch_escalation_virq);
	rthal_exit();
}

#endif /* !_XENO_ASM_SH_BITS_INIT_H */

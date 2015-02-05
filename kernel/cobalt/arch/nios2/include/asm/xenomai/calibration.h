/*
 * Copyright (C) 2009 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _COBALT_NIOS2_ASM_CALIBRATION_H
#define _COBALT_NIOS2_ASM_CALIBRATION_H

static inline void xnarch_get_latencies(struct xnclock_gravity *p)
{
#if CONFIG_XENO_OPT_TIMING_SCHEDLAT != 0
#define __sched_latency CONFIG_XENO_OPT_TIMING_SCHEDLAT
#elif defined(CONFIG_ALTERA_DE2)
#define __sched_latency  10000
#elif defined(CONFIG_NEEK)
#define __sched_latency  10000
#else
#error "unsupported NIOS2 platform"
#endif
	p->user = __sched_latency;
	p->kernel = CONFIG_XENO_OPT_TIMING_KSCHEDLAT;
	p->irq = CONFIG_XENO_OPT_TIMING_IRQLAT;
}

#undef __sched_latency

#endif /* !_COBALT_NIOS2_ASM_CALIBRATION_H */

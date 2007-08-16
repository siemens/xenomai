/*
 * Copyright (C) 2001,2002,2003,2004,2005 Philippe Gerum <rpm@xenomai.org>.
 *
 * 64-bit PowerPC adoption
 *   copyright (C) 2005 Taneli Vähäkangas and Heikki Lindholm
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

#ifndef _XENO_ASM_POWERPC_CALIBRATION_H
#define _XENO_ASM_POWERPC_CALIBRATION_H

#ifndef _XENO_ASM_POWERPC_BITS_INIT_H
#error "please don't include asm/calibration.h directly"
#endif

#include <asm/delay.h>

#define __bogomips (loops_per_jiffy/(500000/HZ))

static inline unsigned long xnarch_get_sched_latency(void)
{
#if CONFIG_XENO_OPT_TIMING_SCHEDLAT != 0
#define __sched_latency CONFIG_XENO_OPT_TIMING_SCHEDLAT
#else


#if defined(CONFIG_PPC_PASEMI)
#ifdef CONFIG_SMP
#define __sched_latency 7000
#else
#define __sched_latency 4000
#endif
#elif defined(CONFIG_PPC_MPC52xx)
#define __sched_latency 7500
#endif

#ifndef __sched_latency
/* Platform is unknown: pick a default value. */
#ifdef CONFIG_PPC64
#define __sched_latency 1000
#else
#define __sched_latency 4000
#endif
#endif

#endif /* CONFIG_XENO_OPT_TIMING_SCHEDLAT */

	return __sched_latency;
}

#undef __sched_latency
#undef __bogomips

#endif /* !_XENO_ASM_POWERPC_CALIBRATION_H */

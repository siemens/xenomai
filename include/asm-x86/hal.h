/**
 * @ingroup hal
 * @file
 *
 * Copyright (C) 2007 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _XENO_ASM_X86_HAL_H
#define _XENO_ASM_X86_HAL_H

#include <linux/ipipe.h>

#ifdef CONFIG_X86_LOCAL_APIC
#ifdef __IPIPE_FEATURE_APIC_TIMER_FREQ
#define RTHAL_COMPAT_TIMERFREQ		__ipipe_apic_timer_freq
#else
/* Fallback value: may be inaccurate. */
#define RTHAL_COMPAT_TIMERFREQ		(apic_read(APIC_TMICT) * HZ)
#endif
#else
#define RTHAL_COMPAT_TIMERFREQ		CLOCK_TICK_RATE
#endif

extern enum rthal_ktimer_mode rthal_ktimer_saved_mode;

void rthal_latency_above_max(struct pt_regs *regs);

#ifdef __i386__
#include "hal_32.h"
#else
#include "hal_64.h"
#endif

#endif /* !_XENO_ASM_X86_HAL_H */

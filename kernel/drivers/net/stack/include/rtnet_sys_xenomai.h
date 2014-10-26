/***
 *
 *  include/rtnet_sys_xenomai.h
 *
 *  RTnet - real-time networking subsystem
 *          RTOS abstraction layer - Xenomai version
 *
 *  Copyright (C) 2004, 2005 Jan Kiszka <jan.kiszka@web.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef __RTNET_SYS_XENOMAI_H_
#define __RTNET_SYS_XENOMAI_H_

#include <nucleus/pod.h>


/* not available in Xenomai 2.0 */
#ifndef RTDM_TASK_RAISE_PRIORITY
# define RTDM_TASK_RAISE_PRIORITY   (+1)
# define RTDM_TASK_LOWER_PRIORITY   (-1)
#endif


#ifdef CONFIG_XENO_2_0x

#define CONFIG_RTOS_STARTSTOP_TIMER 1

static inline int rtos_timer_start(void)
{
    return xnpod_start_timer(XN_APERIODIC_TICK, XNPOD_DEFAULT_TICKHANDLER);
}

static inline void rtos_timer_stop(void)
{
    xnpod_stop_timer();
}

#endif /* CONFIG_XENO_2_0x */


static inline void rtos_irq_release_lock(void)
{
    xnpod_set_thread_mode(xnpod_current_thread(), 0, XNLOCK);
    rthal_local_irq_enable_hw();
}

static inline void rtos_irq_reacquire_lock(void)
{
    rthal_local_irq_disable_hw();
    xnpod_set_thread_mode(xnpod_current_thread(), XNLOCK, 0);
}

#endif /* __RTNET_SYS_XENOMAI_H_ */

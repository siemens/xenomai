/***
 *
 *  include/rtnet_sys_rtai.h
 *
 *  RTnet - real-time networking subsystem
 *          RTOS abstraction layer - RTAI version (3.3 or better)
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

#ifndef __RTNET_SYS_RTAI_H_
#define __RTNET_SYS_RTAI_H_


/* workarounds for namespace pollution of RTAI */
#undef SLEEP
#undef PRINTK


#define CONFIG_RTOS_STARTSTOP_TIMER 1

static inline int rtos_timer_start(void)
{
    rt_set_oneshot_mode();
    start_rt_timer(0);
    return 0;
}

static inline void rtos_timer_stop(void)
{
    stop_rt_timer();
}


static inline void rtos_irq_release_lock(void)
{
    rt_sched_lock();
    hard_sti();
}

static inline void rtos_irq_reacquire_lock(void)
{
    hard_cli();
    rt_sched_unlock();
}

#endif /* __RTNET_SYS_RTAI_H_ */

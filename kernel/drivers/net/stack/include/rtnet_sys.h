/***
 *
 *  include/rtnet_sys.h
 *
 *  RTnet - real-time networking subsystem
 *          RTOS abstraction layer
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

#ifndef __RTNET_SYS_H_
#define __RTNET_SYS_H_

#include <linux/time.h>
#include <linux/types.h>

#include <rtdm/driver.h>


#if defined(CONFIG_RTAI_3x)
/* Support for 3.x */
#include <rtnet_sys_rtai.h>
#elif defined(CONFIG_XENO_2_0x) || defined(CONFIG_XENO_2_1x)
/* Support for Xenomai 2.0 or better */
#include <rtnet_sys_xenomai.h>
#endif


/* common, RTDM-related part */

#if RTDM_API_VER < 4
#define RTDM_IRQTYPE_SHARED         0
#define RTDM_IRQTYPE_EDGE           0
#define RTDM_IRQ_NONE               0
#define RTDM_IRQ_HANDLED            RTDM_IRQ_ENABLE
#endif /* RTDM_API_VER < 4 */

#if RTDM_API_VER < 6
#define rtdm_irq_request(irq_handle, irq, handler, flags, name, device) \
    ({ \
	int err = rtdm_irq_request(irq_handle, irq, handler, flags, name, \
				   device); \
	if (!err) \
	    rtdm_irq_enable(irq_handle); \
	err; \
    })
#define rtdm_nrtsig_init(nrt_sig, handler, arg) \
    rtdm_nrtsig_init(nrt_sig, (rtdm_nrtsig_handler_t)handler)
#define rtdm_task_sleep_abs(wakeup_date, mode) \
    rtdm_task_sleep_until(wakeup_date)
#endif /* RTDM_API_VER < 6 */

#endif /* __RTNET_SYS_H_ */

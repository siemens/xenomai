/**
 * @file
 * Comedi for RTDM, Operation system facilities
 *
 * Copyright (C) 1997-2000 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2008 Alexis Berlemont <alexis.berlemont@free.fr>
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
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __COMEDI_OS_FACILITIES__
#define __COMEDI_OS_FACILITIES__

#if defined(__KERNEL__) && !defined(DOXYGEN_CPP)

#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <asm/uaccess.h>

#if !(defined(CONFIG_XENO_SKIN_RTDM) || \
      defined(CONFIG_XENO_SKIN_RTDM_MODULE))
#error Comedi4RTDM needs RTDM enabled \
    (statically or as amodule) to compile properly
#endif /* !(CONFIG_XENO_SKIN_RTDM || 
          CONFIG_XENO_SKIN_RTDM_MODULE */

#include <rtdm/rtdm_driver.h>

/* --- Kernel traces functions --- */

#define COMEDI_PROMPT "Comedi: "

#define RTDM_SUBCLASS_COMEDI 0

#define __comedi_logerr(fmt, args...) rtdm_printk(KERN_ERR COMEDI_PROMPT fmt, ##args)
#define __comedi_loginfo(fmt, args...) rtdm_printk(KERN_INFO COMEDI_PROMPT fmt, ##args)

#define comedi_logerr(fmt, args...) __comedi_logerr(fmt, ##args)

#ifdef COMEDI_DEBUG
#define comedi_loginfo(fmt, args...) __comedi_loginfo(fmt, ##args)
#else /* !COMEDI_DEBUG */
#define comedi_loginfo(fmt, args...)
#endif /* COMEDI_DEBUG */

/* --- Allocation / MMU section --- */

static inline void *comedi_kmalloc(int size)
{
	return rtdm_malloc(size);
}

static inline void comedi_kfree(void *pinp)
{
	rtdm_free(pinp);
}

static inline int __comedi_copy_from_user(rtdm_user_info_t * user_info,
					  void *pind, void *pins, int size)
{
	if (rtdm_read_user_ok(user_info, pins, size))
		return rtdm_copy_from_user(user_info, pind, pins, size);
	else
		return -EFAULT;
}

static inline int __comedi_copy_to_user(rtdm_user_info_t * user_info,
					void *pind, void *pins, int size)
{
	if (rtdm_rw_user_ok(user_info, pind, size))
		return rtdm_copy_to_user(user_info, pind, pins, size);
	else
		return -EFAULT;
}

/* --- Spinlock section --- */

typedef rtdm_lock_t comedi_lock_t;

#define COMEDI_LOCK_UNLOCKED RTDM_LOCK_UNLOCKED

#define comedi_lock_init(lock) rtdm_lock_init(lock)
#define comedi_lock(lock) rtdm_lock_get(lock)
#define comedi_unlock(lock) rtdm_lock_put(lock)
#define comedi_lock_irqsave(lock, context)	\
    rtdm_lock_get_irqsave(lock, context)
#define comedi_unlock_irqrestore(lock, context) \
    rtdm_lock_put_irqrestore(lock, context)

/* --- Task section --- */

#define COMEDI_TASK_LOWEST_PRIORITY RTDM_TASK_LOWEST_PRIORITY
#define COMEDI_TASK_HIGHEST_PRIORITY RTDM_TASK_HIGHEST_PRIORITY

typedef rtdm_task_t comedi_task_t;
typedef rtdm_task_proc_t comedi_task_proc_t;

#define comedi_task_init(tsk, name, proc, arg, priority) \
    rtdm_task_init(tsk, name, proc, arg, priority, 0)

#define comedi_task_destroy(tsk) rtdm_task_destroy(tsk)

#define comedi_task_sleep(delay) rtdm_task_sleep(delay)

/* --- Time section --- */

static inline void comedi_udelay(unsigned int us)
{
	rtdm_task_busy_sleep(((nanosecs_rel_t) us) * 1000);
}

static inline unsigned long long comedi_get_rawtime(void)
{
	return rtdm_clock_read();
}

/* Function which gives absolute time */
unsigned long long comedi_get_time(void);

/* Function for setting up the absolute time recovery */
void comedi_init_time(void);

/* --- IRQ section --- */

#define COMEDI_IRQ_SHARED RTDM_IRQTYPE_SHARED
#define COMEDI_IRQ_EDGE RTDM_IRQTYPE_EDGE
#define COMEDI_IRQ_DISABLED 0

typedef int (*comedi_irq_hdlr_t) (unsigned int irq, void *d);

struct comedi_irq_descriptor {
	/* These fields are useful to launch the IRQ trampoline;
	   that is the reason why a structure has been defined */
	comedi_irq_hdlr_t handler;
	unsigned int irq;
	void *cookie;
	rtdm_irq_t rtdm_desc;
};
typedef struct comedi_irq_descriptor comedi_irq_desc_t;

int __comedi_request_irq(comedi_irq_desc_t * dsc,
			 unsigned int irq,
			 comedi_irq_hdlr_t handler,
			 unsigned long flags, void *cookie);
int __comedi_free_irq(comedi_irq_desc_t * dsc);

/* --- Synchronization section --- */

#define __NRT_WAITER 1
#define __RT_WAITER 2
#define __EVT_PDING 3

struct comedi_sync {
	unsigned long status;
	rtdm_event_t rtdm_evt;
	rtdm_nrtsig_t nrt_sig;
	wait_queue_head_t wq;
};
typedef struct comedi_sync comedi_sync_t;

#define comedi_select_sync(snc, slr, type, fd) \
	rtdm_event_select_bind(&((snc)->rtdm_evt), slr, type, fd)

int comedi_init_sync(comedi_sync_t * snc);
void comedi_cleanup_sync(comedi_sync_t * snc);
int comedi_wait_sync(comedi_sync_t * snc, int rt);
int comedi_timedwait_sync(comedi_sync_t * snc,
			  int rt, unsigned long long ns_timeout);
void comedi_signal_sync(comedi_sync_t * snc);

/* --- Misc section --- */

#define comedi_test_rt() rtdm_in_rt_context()

#endif /* __KERNEL__ && !DOXYGEN_CPP */

#endif /* __COMEDI_OS_FACILITIES__ */

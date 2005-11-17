/**
 *
 * @note Copyright (C) 2004 Philippe Gerum <rpm@xenomai.org> 
 * @note Copyright (C) 2005 Nextream France S.A.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _RTAI_TASK_H
#define _RTAI_TASK_H

#include <xenomai/nucleus/core.h>
#include <xenomai/nucleus/thread.h>
#include <xenomai/nucleus/synch.h>
#include <xenomai/rtai/types.h>

typedef struct rt_task_placeholder {
    rt_handle_t opaque;
    unsigned long opaque2;
} RT_TASK_PLACEHOLDER;

#define RT_HIGHEST_PRIORITY	1

#if defined(__KERNEL__) || defined(__XENO_SIM__)

#define RTAI_TASK_MAGIC 0x17170101

typedef struct rt_task_struct {

    unsigned magic;   /* !< Magic code - must be first */

    xnholder_t link;

#define link2rtask(laddr) \
((RT_TASK *)(((char *)laddr) - (int)(&((RT_TASK *)0)->link)))

    xntimer_t timer;

    xnthread_t thread_base;

    int suspend_depth;

    xnarch_cpumask_t affinity;

    int cookie;

    void (*body)(int cookie);

    void (*sigfn)(void);

} RT_TASK;

static inline RT_TASK *thread2rtask (xnthread_t *t)
{
    return t ? ((RT_TASK *)(((char *)(t)) - (int)(&((RT_TASK *)0)->thread_base))) : NULL;
}

#define rtai_current_task() thread2rtask(xnpod_current_thread())

#ifdef __cplusplus
extern "C" {
#endif

int __rtai_task_pkg_init(void);

void __rtai_task_pkg_cleanup(void);

#ifdef __cplusplus
}
#endif

#include <linux/kernel.h>
#define rt_printk printk
#define rtai_print_to_screen printk	/* FIXME */

#else /* !(__KERNEL__ || __XENO_SIM__) */

typedef RT_TASK_PLACEHOLDER RT_TASK;

#endif /* __KERNEL__ || __XENO_SIM__ */

#ifdef __cplusplus
extern "C" {
#endif

/* Public interface */

int rt_task_init(RT_TASK *task,
		 void (*body)(int),
		 int cookie,
		 int stack_size,
		 int priority,
		 int uses_fpu,
		 void (*sigfn)(void));

int rt_task_make_periodic(RT_TASK *task,
			  RTIME start_time,
			  RTIME period);

int rt_task_make_periodic_relative_ns(RT_TASK *task,
				      RTIME start_delay,
				      RTIME period);
void rt_task_wait_period(void);

int rt_task_suspend(RT_TASK *task);

int rt_task_resume(RT_TASK *task);

int rt_task_delete(RT_TASK *task);

#ifdef __cplusplus
}
#endif

#endif /* !_RTAI_TASK_H */

/*
 * Copyright (C) 2011 Philippe Gerum <rpm@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#ifndef _XENOMAI_ALCHEMY_TASK_H
#define _XENOMAI_ALCHEMY_TASK_H

#include <sys/types.h>
#include <stdint.h>
#include <xeno_config.h>
#include <alchemy/timer.h>

#define T_LOPRIO  0
#define T_HIPRIO  99

#define T_LOCK    0x1
#define T_NOSIG   0x2
#define T_SUSP    0x4
#define T_WARNSW  0x0		/* ??? */
#define T_FPU     0x0		/* Deprecated. */

#define T_CPU(cpu) (1 << (24 + (cpu & 7))) /* Up to 8 cpus [0-7] */
#define T_CPUMASK  0xff000000

struct RT_TASK {
	uintptr_t handle;
};

typedef struct RT_TASK RT_TASK;

static const RT_TASK no_alchemy_task = { .handle = 0 };

#ifdef __cplusplus
extern "C" {
#endif

int rt_task_create(RT_TASK *task,
		   const char *name,
		   int stksize,
		   int prio,
		   int mode);

int rt_task_delete(RT_TASK *task);

int rt_task_start(RT_TASK *task,
		  void (*entry)(void *arg),
		  void *arg);

int rt_task_spawn(RT_TASK *task, const char *name,
		  int stksize, int prio, int mode,
		  void (*entry)(void *arg),
		  void *arg);

int rt_task_shadow(RT_TASK *task,
		   const char *name,
		   int prio,
		   int mode);

int rt_task_set_periodic(RT_TASK *task,
			 RTIME idate, RTIME period);

int rt_task_wait_period(unsigned long *overruns_r);

int rt_task_sleep(RTIME delay);

int rt_task_sleep_until(RTIME date);

int rt_task_same(RT_TASK *task1, RT_TASK *task2);

int rt_task_suspend(RT_TASK *task);

int rt_task_resume(RT_TASK *task);

RT_TASK *rt_task_self(void);

int rt_task_set_priority(RT_TASK *task, int prio);

#ifdef __cplusplus
}
#endif

#endif /* _XENOMAI_ALCHEMY_TASK_H */

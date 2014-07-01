/*
 * Copyright (C) 2014 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _XENOMAI_TRANK_NATIVE_TASK_H
#define _XENOMAI_TRANK_NATIVE_TASK_H

#include <errno.h>
#include <alchemy/task.h>
#include <trank/native/types.h>

#define T_FPU    0
#define T_NOSIG  0

__attribute__((__deprecated__))
static inline int rt_task_notify(RT_TASK *task, rt_sigset_t sigs)
{
	warning("in-kernel native API is gone, rebase over RTDM");
	return -ENOSYS;
}

__attribute__((__deprecated__))
static inline int T_CPU(int cpu)
{
	warning("use rt_task_set_affinity() instead");
	return 0;
}

#endif /* _XENOMAI_TRANK_NATIVE_TASK_H */

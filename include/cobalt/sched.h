/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _COBALT_SCHED_H
#define _COBALT_SCHED_H

#ifdef __KERNEL__

#include <nucleus/sched.h>
#include <linux/sched.h>

#define SCHED_OTHER 0

#else /* !__KERNEL__ */

#include_next <sched.h>
#include <cobalt/wrappers.h>

COBALT_DECL(int, sched_yield(void));

COBALT_DECL(int, sched_get_priority_min(int policy));

COBALT_DECL(int, sched_get_priority_max(int policy));

#ifndef CPU_COUNT
#define CPU_COUNT(__setp)    __PROVIDE_CPU_COUNT(__setp)
#define __PROVIDE_CPU_COUNT(__setp)  __sched_cpucount(sizeof(cpu_set_t), __setp)
int __sched_cpucount(size_t __setsize, const cpu_set_t *__setp);
#endif /* !CPU_COUNT */

#endif /* !__KERNEL__ */

#ifndef __sched_extensions_defined
#define __sched_extensions_defined

#ifndef SCHED_SPORADIC
#define SCHED_SPORADIC		10
#define sched_ss_low_priority	u.ss.__sched_low_priority
#define sched_ss_repl_period	u.ss.__sched_repl_period
#define sched_ss_init_budget	u.ss.__sched_init_budget
#define sched_ss_max_repl	u.ss.__sched_max_repl
#endif	/* !SCHED_SPORADIC */

#define SCHED_COBALT		42

#define sched_rr_quantum	u.rr.__sched_rr_quantum

struct __sched_ss_param {
	int __sched_low_priority;
	struct timespec __sched_repl_period;
	struct timespec __sched_init_budget;
	int __sched_max_repl;
};

struct __sched_rr_param {
	struct timespec __sched_rr_quantum;
};

struct sched_param_ex {
	int sched_priority;
	union {
		struct __sched_ss_param ss;
		struct __sched_rr_param rr;
	} u;
};

#endif /* __sched_extensions_defined */

#endif /* !_COBALT_SCHED_H */

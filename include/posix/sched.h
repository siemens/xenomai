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

#ifndef _XENO_POSIX_SCHED_H
#define _XENO_POSIX_SCHED_H

#if defined(__KERNEL__) || defined(__XENO_SIM__)

#include <nucleus/xenomai.h>

#ifdef __KERNEL__
#include <linux/sched.h>
#endif /* __KERNEL__ */

#ifdef __XENO_SIM__
#include <posix_overrides.h>
#define SCHED_FIFO	1
#define SCHED_RR	2
#endif /* __XENO_SIM__ */

#define SCHED_OTHER 0

struct timespec;

#ifdef __cplusplus
extern "C" {
#endif

int sched_yield(void);

int sched_get_priority_min(int policy);

int sched_get_priority_max(int policy);

int sched_rr_get_interval(int pid, struct timespec *interval);

#ifdef __cplusplus
}
#endif

#else /* !(__KERNEL__ || __XENO_SIM__) */

#include_next <sched.h>

int __real_sched_yield(void);

#endif /* !(__KERNEL__ || __XENO_SIM__) */

#ifndef __sched_extensions_defined
#define __sched_extensions_defined

#ifndef SCHED_SPORADIC
#define SCHED_SPORADIC		10
#define sched_ss_low_priority	ss.__sched_low_priority
#define sched_ss_repl_period	ss.__sched_repl_period
#define sched_ss_init_budget	ss.__sched_init_budget
#define sched_ss_max_repl	ss.__sched_max_repl
#endif	/* !SCHED_SPORADIC */

struct __sched_ss_param {
	int __sched_low_priority;
	struct timespec __sched_repl_period;
	struct timespec __sched_init_budget;
	int __sched_max_repl;
};

struct sched_param_ex {
	int sched_priority;
	struct __sched_ss_param ss;
};

#endif /* __sched_extensions_defined */

#endif /* SCHED_H */

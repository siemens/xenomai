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
#ifndef _COBALT_POSIX_TIMER_H
#define _COBALT_POSIX_TIMER_H

#include <linux/time.h>

struct cobalt_thread;

int cobalt_timer_create(clockid_t clock,
			const struct sigevent __user *u_sev,
			timer_t __user *u_tm);

int cobalt_timer_delete(timer_t tm);

int cobalt_timer_settime(timer_t tm,
			 int flags,
			 const struct itimerspec __user *u_newval,
			 struct itimerspec __user *u_oldval);

int cobalt_timer_gettime(timer_t tm, struct itimerspec __user *u_val);

int cobalt_timer_getoverrun(timer_t tm);

void cobalt_timer_notified(timer_t timerid);

void cobalt_timer_flush(struct cobalt_thread *zombie);

void cobalt_timerq_cleanup(struct cobalt_kqueues *q);

int cobalt_timer_pkg_init(void);

void cobalt_timer_pkg_cleanup(void);

#endif /* !_COBALT_POSIX_TIMER_H */

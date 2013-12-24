/*
 * Copyright (C) 2013 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _COBALT_POSIX_PROCESS_H
#define _COBALT_POSIX_PROCESS_H

#include <linux/list.h>
#include <linux/bitmap.h>
#include <cobalt/kernel/ppd.h>

struct cobalt_kqueues {
	struct list_head condq;
	struct list_head mutexq;
	struct list_head semq;
	struct list_head threadq;
	struct list_head monitorq;
	struct list_head eventq;
};

struct cobalt_timer;
struct cobalt_process {
	struct cobalt_kqueues kqueues;
	struct rb_root usems;
	struct list_head sigwaiters;
	DECLARE_BITMAP(timers_map, CONFIG_XENO_OPT_NRTIMERS);
	struct cobalt_timer *timers[CONFIG_XENO_OPT_NRTIMERS];
};

extern struct cobalt_kqueues cobalt_global_kqueues;

#endif /* !_COBALT_POSIX_PROCESS_H */

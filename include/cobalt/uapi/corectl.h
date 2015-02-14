/*
 * Copyright (C) 2015 Philippe Gerum <rpm@xenomai.org>.
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
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */
#ifndef _COBALT_UAPI_CORECTL_H
#define _COBALT_UAPI_CORECTL_H

#define _CC_COBALT_GET_VERSION		0
#define _CC_COBALT_GET_NR_PIPES		1
#define _CC_COBALT_GET_NR_TIMERS	2

#define _CC_COBALT_GET_DEBUG		3
#   define _CC_COBALT_DEBUG_ASSERT	1
#   define _CC_COBALT_DEBUG_CONTEXT	2
#   define _CC_COBALT_DEBUG_LOCKING	4
#   define _CC_COBALT_DEBUG_USER	8
#   define _CC_COBALT_DEBUG_RELAX	16

#define _CC_COBALT_GET_POLICIES		4
#   define _CC_COBALT_SCHED_FIFO	1
#   define _CC_COBALT_SCHED_RR		2
#   define _CC_COBALT_SCHED_WEAK	4
#   define _CC_COBALT_SCHED_SPORADIC	8
#   define _CC_COBALT_SCHED_QUOTA	16
#   define _CC_COBALT_SCHED_TP		32

#define _CC_COBALT_GET_WATCHDOG		5
#define _CC_COBALT_GET_CORE_STATUS	6
#define _CC_COBALT_START_CORE		7
#define _CC_COBALT_STOP_CORE		8

enum cobalt_run_states {
	COBALT_STATE_DISABLED,
	COBALT_STATE_RUNNING,
	COBALT_STATE_STOPPED,
	COBALT_STATE_TEARDOWN,
	COBALT_STATE_WARMUP,
};

#endif /* !_COBALT_UAPI_CORECTL_H */

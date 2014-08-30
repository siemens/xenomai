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
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */
#ifndef _COBALT_UAPI_SYSCONF_H
#define _COBALT_UAPI_SYSCONF_H

#define _SC_COBALT_VERSION	0
#define _SC_COBALT_NR_PIPES	1
#define _SC_COBALT_NR_TIMERS	2
#define _SC_COBALT_DEBUG	3
#   define _SC_COBALT_DEBUG_ASSERT	1
#   define _SC_COBALT_DEBUG_CONTEXT	2
#   define _SC_COBALT_DEBUG_LOCKING	4
#   define _SC_COBALT_DEBUG_SYNCREL	8
#   define _SC_COBALT_DEBUG_TRACEREL	16
#define _SC_COBALT_POLICIES	4
#   define _SC_COBALT_SCHED_FIFO	1
#   define _SC_COBALT_SCHED_RR		2
#   define _SC_COBALT_SCHED_WEAK	4
#   define _SC_COBALT_SCHED_SPORADIC	8
#   define _SC_COBALT_SCHED_QUOTA	16
#   define _SC_COBALT_SCHED_TP		32
#define _SC_COBALT_WATCHDOG	5
#define _SC_COBALT_EXTENSION	6

#endif /* !_COBALT_UAPI_SYSCONF_H */

/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _COBALT_UAPI_THREAD_H
#define _COBALT_UAPI_THREAD_H

#include <cobalt/uapi/kernel/thread.h>

#define PTHREAD_WARNSW      XNTRAPSW
#define PTHREAD_LOCK_SCHED  XNLOCK
#define PTHREAD_CONFORMING  0

struct cobalt_mutexattr {
	unsigned int magic: 24;
	unsigned int type: 2;
	unsigned int protocol: 2;
	unsigned int pshared: 1;
};

struct cobalt_condattr {
	unsigned int magic: 24;
	unsigned int clock: 2;
	unsigned int pshared: 1;
};

struct cobalt_threadstat {
	int cpu;
	unsigned long status;
	unsigned long long xtime;
	unsigned long msw;
	unsigned long csw;
	unsigned long xsc;
	unsigned long pf;
	unsigned long long timeout;
};

#endif /* !_COBALT_UAPI_THREAD_H */

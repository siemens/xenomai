/*
 * Copyright (C) 2012 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _MERCURY_WRAPPERS_H
#define _MERCURY_WRAPPERS_H

#include <xeno_config.h>

#define __RT(call)	call
#define __STD(call)	call

#ifndef HAVE_PTHREAD_CONDATTR_SETCLOCK

#include <pthread.h>

static inline
int pthread_condattr_setclock(pthread_condattr_t *attr,
			      clockid_t clk_id)
{
	return ENOSYS;
}

#endif /* !HAVE_PTHREAD_CONDATTR_SETCLOCK */

#ifndef HAVE_PTHREAD_MUTEXATTR_SETPROTOCOL

enum
{
  PTHREAD_PRIO_NONE,
  PTHREAD_PRIO_INHERIT,
  PTHREAD_PRIO_PROTECT
};

static inline
int pthread_mutexattr_setprotocol(pthread_mutexattr_t *attr, int protocol)
{
	return ENOSYS;
}

#endif /* !HAVE_PTHREAD_MUTEXATTR_SETPROTOCOL */

#endif /* _MERCURY_WRAPPERS_H */

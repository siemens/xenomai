/*
 * Copyright (C) 2013 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _BOILERPLATE_SCOPE_H
#define _BOILERPLATE_SCOPE_H

#include <sys/types.h>
#include <stddef.h>
#include <stdint.h>
#include <xeno_config.h>

typedef uintptr_t memoff_t;

#ifdef CONFIG_XENO_PSHARED

extern void *__main_heap;

int pshared_check(void *heap, void *addr);

#define dref_type(t)		memoff_t
#define __memoff(base, addr)	((caddr_t)(addr) - (caddr_t)(base))
#define __memptr(base, off)	((caddr_t)(base) + (off))
#define __memchk(base, addr)	pshared_check(base, addr)

#define mutex_scope_attribute	PTHREAD_PROCESS_SHARED
#define sem_scope_attribute	1
#ifdef CONFIG_XENO_COBALT
#define monitor_scope_attribute	COBALT_MONITOR_SHARED
#define event_scope_attribute	COBALT_EVENT_SHARED
#endif

#else /* !CONFIG_XENO_PSHARED */

#define __main_heap	NULL

#define dref_type(t)		__typeof__(t)
#define __memoff(base, addr)	(addr)
#define __memptr(base, off)	(off)
#define __memchk(base, addr)	1

#define mutex_scope_attribute	PTHREAD_PROCESS_PRIVATE
#define sem_scope_attribute	0
#ifdef CONFIG_XENO_COBALT
#define monitor_scope_attribute	0
#define event_scope_attribute	0
#endif

#endif /* !CONFIG_XENO_PSHARED */

#endif /* _BOILERPLATE_SCOPE_H */

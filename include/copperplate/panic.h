/*
 * Copyright (C) 2008 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _COPPERPLATE_PANIC_H
#define _COPPERPLATE_PANIC_H

#include <stdarg.h>
#include <pthread.h>

struct threadobj;
struct error_frame;

#ifdef __cplusplus
extern "C" {
#endif

void error_hook(struct error_frame *ef);

void __printout(struct threadobj *thobj,
		const char *header,
		const char *fmt, va_list ap);

void panic(const char *fmt, ...);

void warning(const char *fmt, ...);

const char *symerror(int errnum);

extern const char *dashes;

extern pthread_mutex_t __printlock;

#ifdef __cplusplus
}
#endif

#endif /* _COPPERPLATE_PANIC_H */

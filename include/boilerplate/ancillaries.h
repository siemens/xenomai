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
#ifndef _BOILERPLATE_ANCILLARIES_H
#define _BOILERPLATE_ANCILLARIES_H

#include <stdarg.h>
#include <time.h>
#include <pthread.h>

extern struct timespec __init_date;

extern pthread_mutex_t __printlock;

struct error_frame;
struct cleanup_block;

#ifdef __cplusplus
extern "C" {
#endif

void __run_cleanup_block(struct cleanup_block *cb);

void __printout(const char *name,
		const char *header,
		const char *fmt, va_list ap);

void __panic(const char *name,
	     const char *fmt, va_list ap);

void panic(const char *fmt, ...);

void __warning(const char *name,
	       const char *fmt, va_list ap);

void warning(const char *fmt, ...);

const char *symerror(int errnum);

void error_hook(struct error_frame *ef);

void boilerplate_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _BOILERPLATE_ANCILLARIES_H */

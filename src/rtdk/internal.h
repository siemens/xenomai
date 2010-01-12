/*
 * Copyright (C) 2007 Jan Kiszka <jan.kiszka@web.de>.
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

#ifndef _LIBRTUTILS_INTERNAL_H
#define _LIBRTUTILS_INTERNAL_H

#include <time.h>
#include <sys/time.h>

void __rt_print_init(void);
void __rt_print_exit(void);

void __real_free(void *ptr);
void *__real_malloc(size_t size);

int __real_gettimeofday(struct timeval *tv, struct timezone *tz);
int __real_clock_gettime(clockid_t clk_id, struct timespec *tp);

#endif /* !_LIBRTUTILS_INTERNAL_H */

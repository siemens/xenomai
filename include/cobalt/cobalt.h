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
#ifndef _COBALT_COBALT_H
#define _COBALT_COBALT_H

#include <sys/types.h>
#include <cobalt/uapi/thread.h>

#define cobalt_commit_memory(p) __cobalt_commit_memory(p, sizeof(*p))

#ifdef __cplusplus
extern "C" {
#endif

int cobalt_thread_stat(pid_t pid,
		       struct cobalt_threadstat *stat);

int cobalt_serial_debug(const char *fmt, ...);

size_t cobalt_get_stacksize(size_t size);

void __cobalt_commit_memory(void *p, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* !_COBALT_COBALT_H */

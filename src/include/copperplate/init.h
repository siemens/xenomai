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

#ifndef _COPPERPLATE_INIT_H
#define _COPPERPLATE_INIT_H

#include <sched.h>

extern unsigned int __tick_period_arg;

extern unsigned int __mem_pool_arg;

extern int __no_mlock_arg;

extern char __registry_mountpt_arg[];

extern int __no_registry_arg;

extern const char *__session_label_arg;

extern int __reset_session_arg;

extern cpu_set_t __cpu_affinity;

#ifdef __cplusplus
extern "C" {
#endif

int copperplate_init(int argc, char *const argv[]);

void panic(const char *fmt, ...);

void warning(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* _COPPERPLATE_INIT_H */

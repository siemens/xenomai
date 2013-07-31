/*
 * Copyright (C) 2006 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _COBALT_KERNEL_ASSERT_H
#define _COBALT_KERNEL_ASSERT_H

#include <cobalt/kernel/trace.h>

#define XENO_DEBUG(subsystem)			\
	(CONFIG_XENO_OPT_DEBUG_##subsystem > 0)

#define XENO_ASSERT(subsystem,cond,action)  do {			\
		if (unlikely(XENO_DEBUG(subsystem) && !(cond))) {	\
			xntrace_panic_freeze();				\
			printk(XENO_ERR "assertion failed at %s:%d (%s)\n",	\
				 __FILE__, __LINE__, (#cond));		\
			xntrace_panic_dump();			\
			action;						\
		}							\
	} while(0)

#define XENO_BUGON(subsystem,cond)					\
	do {								\
		if (unlikely(XENO_DEBUG(subsystem) && (cond)))		\
			xnpod_fatal("bug at %s:%d (%s)",		\
				    __FILE__, __LINE__, (#cond));	\
	} while(0)

#ifndef CONFIG_XENO_OPT_DEBUG_NUCLEUS
#define CONFIG_XENO_OPT_DEBUG_NUCLEUS 0
#endif /* CONFIG_XENO_OPT_DEBUG_NUCLEUS */

extern void (*nkpanic)(const char *format, ...);

#define xnpod_fatal(__fmt, __args...) nkpanic(__fmt, ##__args)

#endif /* !_COBALT_KERNEL_ASSERT_H */

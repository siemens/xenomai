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

#define XENO_INFO KERN_INFO    "[Xenomai] "
#define XENO_WARN KERN_WARNING "[Xenomai] "
#define XENO_ERR  KERN_ERR     "[Xenomai] "

#define XENO_DEBUG(__subsys)	\
	(CONFIG_XENO_OPT_DEBUG_##__subsys > 0)

#define XENO_ASSERT(__subsys, __cond)						\
	({									\
		int __ret = !XENO_DEBUG(__subsys) || (__cond);			\
		if (unlikely(!__ret))						\
			__xnsys_assert_failed(__FILE__, __LINE__, (#__cond));	\
		__ret;								\
	})

#define XENO_BUGON(__subsys, __cond)					\
	do {								\
		if (unlikely(XENO_DEBUG(__subsys) && (__cond)))		\
			xnsys_fatal("bug at %s:%d (%s)",		\
				    __FILE__, __LINE__, (#__cond));	\
	} while (0)

#ifndef CONFIG_XENO_OPT_DEBUG_NUCLEUS
#define CONFIG_XENO_OPT_DEBUG_NUCLEUS 0
#endif /* CONFIG_XENO_OPT_DEBUG_NUCLEUS */

void __xnsys_assert_failed(const char *file, int line, const char *msg);

void __xnsys_fatal(const char *format, ...);

#define xnsys_fatal(__fmt, __args...) nkpanic(__fmt, ##__args)

extern void (*nkpanic)(const char *format, ...);

#endif /* !_COBALT_KERNEL_ASSERT_H */

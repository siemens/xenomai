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
#include <cobalt/kernel/ancillaries.h>

#define XENO_INFO  KERN_INFO    "[Xenomai] "
#define XENO_WARN  KERN_WARNING "[Xenomai] "
#define XENO_ERR   KERN_ERR     "[Xenomai] "

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

#define XENO_BUG(__subsys)  XENO_BUGON(__subsys, 1)

#define XENO_WARNON(__subsys, __cond)					\
	do {								\
		if (unlikely(XENO_DEBUG(__subsys) && (__cond)))		\
			printk(XENO_WARN "assertion failed at %s:%d (%s)", \
				    __FILE__, __LINE__, (#__cond));	\
	} while (0)

#ifndef CONFIG_XENO_OPT_DEBUG_NUCLEUS
#define CONFIG_XENO_OPT_DEBUG_NUCLEUS 0
#endif

#ifndef CONFIG_XENO_OPT_DEBUG_CONTEXT
#define CONFIG_XENO_OPT_DEBUG_CONTEXT 0
#endif

#ifndef CONFIG_XENO_OPT_DEBUG_LOCKING
#define CONFIG_XENO_OPT_DEBUG_LOCKING 0
#endif

#ifndef CONFIG_XENO_OPT_DEBUG_SYNCH_RELAX
#define CONFIG_XENO_OPT_DEBUG_SYNCH_RELAX 0
#endif

#ifndef CONFIG_XENO_OPT_DEBUG_RTDM
#define CONFIG_XENO_OPT_DEBUG_RTDM	0
#endif

#ifndef CONFIG_XENO_OPT_DEBUG_RTDM_APPL
#define CONFIG_XENO_OPT_DEBUG_RTDM_APPL	0
#endif

#ifndef CONFIG_XENO_OPT_DEBUG_COBALT
#define CONFIG_XENO_OPT_DEBUG_COBALT 0
#endif

#define primary_mode_only()	XENO_BUGON(CONTEXT, ipipe_root_p)
#define secondary_mode_only()	XENO_BUGON(CONTEXT, !ipipe_root_p)
#define interrupt_only()	XENO_BUGON(CONTEXT, !xnsched_interrupt_p())
#define realtime_cpu_only()	XENO_BUGON(CONTEXT, !xnsched_supported_cpu(ipipe_processor_id()))
#define thread_only()		XENO_BUGON(CONTEXT, xnsched_interrupt_p())
#if XENO_DEBUG(LOCKING)
#define atomic_only()		XENO_BUGON(CONTEXT, (xnlock_is_owner(&nklock) && hard_irqs_disabled()) == 0)
#define preemptible_only()	XENO_BUGON(CONTEXT, xnlock_is_owner(&nklock) || hard_irqs_disabled())
#else
#define atomic_only()		XENO_BUGON(CONTEXT, hard_irqs_disabled() == 0)
#define preemptible_only()	XENO_BUGON(CONTEXT, hard_irqs_disabled() != 0)
#endif

void __xnsys_assert_failed(const char *file, int line, const char *msg);

void __xnsys_fatal(const char *format, ...);

#define xnsys_fatal(__fmt, __args...) nkpanic(KERN_ERR __fmt, ##__args)

extern void (*nkpanic)(const char *format, ...);

#endif /* !_COBALT_KERNEL_ASSERT_H */

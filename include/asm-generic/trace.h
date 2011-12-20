/*
 * Copyright (C) 2007 Jan Kiszka <jan.kiszka@siemens.com>.
 * Copyright (C) 2011 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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

#ifndef _XENO_ASM_GENERIC_TRACE_H
#define _XENO_ASM_GENERIC_TRACE_H

#ifdef CONFIG_IPIPE_TRACE

#include <linux/ipipe_trace.h>

static inline int xnarch_trace_max_begin(unsigned long v)
{
	ipipe_trace_begin(v);
	return 0;
}

static inline int xnarch_trace_max_end(unsigned long v)
{
	ipipe_trace_end(v);
	return 0;
}

static inline int xnarch_trace_max_reset(void)
{
	ipipe_trace_max_reset();
	return 0;
}

static inline int xnarch_trace_user_start(void)
{
	return ipipe_trace_frozen_reset();
}

static inline int xnarch_trace_user_stop(unsigned long v)
{
	ipipe_trace_freeze(v);
	return 0;
}

static inline int xnarch_trace_user_freeze(unsigned long v, int once)
{
	int ret = 0;

	if (!once)
		ret = ipipe_trace_frozen_reset();

	ipipe_trace_freeze(v);

	return ret;
}

static inline int xnarch_trace_special(unsigned char id, unsigned long v)
{
	ipipe_trace_special(id, v);
	return 0;
}

static inline int xnarch_trace_special_u64(unsigned char id,
					  unsigned long long v)
{
	ipipe_trace_special(id, (unsigned long)(v >> 32));
	ipipe_trace_special(id, (unsigned long)(v & 0xFFFFFFFF));
	return 0;
}

static inline int xnarch_trace_pid(pid_t pid, short prio)
{
	ipipe_trace_pid(pid, prio);
	return 0;
}

static inline int xnarch_trace_tick(unsigned long delay_tsc)
{
	ipipe_trace_event(0, delay_tsc);
	return 0;
}

static inline int xnarch_trace_panic_freeze(void)
{
	ipipe_trace_panic_freeze();
	return 0;
}

static inline int xnarch_trace_panic_dump(void)
{
	ipipe_trace_panic_dump();
	return 0;
}

#else /* !CONFIG_IPIPE_TRACE */

#define xnarch_trace_max_begin(v)		({ int ret = -ENOSYS; ret; })
#define xnarch_trace_max_end(v)			({ int ret = -ENOSYS; ret; })
#define xnarch_trace_max_reset(v)		({ int ret = -ENOSYS; ret; })
#define xnarch_trace_user_start()		({ int ret = -ENOSYS; ret; })
#define xnarch_trace_user_stop(v)		({ int ret = -ENOSYS; ret; })
#define xnarch_trace_user_freeze(v, once)	({ int ret = -ENOSYS; ret; })
#define xnarch_trace_special(id, v)		({ int ret = -ENOSYS; ret; })
#define xnarch_trace_special_u64(id, v)		({ int ret = -ENOSYS; ret; })
#define xnarch_trace_pid(pid, prio)		({ int ret = -ENOSYS; ret; })
#define xnarch_trace_tick(delay_tsc)		({ int ret = -ENOSYS; ret; })
#define xnarch_trace_panic_freeze()		({ int ret = -ENOSYS; ret; })
#define xnarch_trace_panic_dump()		({ int ret = -ENOSYS; ret; })

#endif /* CONFIG_IPIPE_TRACE */

#endif /* !_XENO_ASM_GENERIC_TRACE_H */

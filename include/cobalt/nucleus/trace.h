/*
 * Copyright (C) 2006 Jan Kiszka <jan.kiszka@web.de>.
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
 *
 * User-space interface to the arch-specific tracing support.
 */

#ifndef _XENO_NUCLEUS_TRACE_H
#define _XENO_NUCLEUS_TRACE_H

#define __xntrace_op_max_begin		0
#define __xntrace_op_max_end		1
#define __xntrace_op_max_reset		2
#define __xntrace_op_user_start		3
#define __xntrace_op_user_stop		4
#define __xntrace_op_user_freeze	5
#define __xntrace_op_special		6
#define __xntrace_op_special_u64	7

#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/ipipe_trace.h>

static inline int xntrace_max_begin(unsigned long v)
{
	ipipe_trace_begin(v);
	return 0;
}

static inline int xntrace_max_end(unsigned long v)
{
	ipipe_trace_end(v);
	return 0;
}

static inline int xntrace_max_reset(void)
{
	ipipe_trace_max_reset();
	return 0;
}

static inline int xntrace_user_start(void)
{
	return ipipe_trace_frozen_reset();
}

static inline int xntrace_user_stop(unsigned long v)
{
	ipipe_trace_freeze(v);
	return 0;
}

static inline int xntrace_user_freeze(unsigned long v, int once)
{
	int ret = 0;

	if (!once)
		ret = ipipe_trace_frozen_reset();

	ipipe_trace_freeze(v);

	return ret;
}

static inline int xntrace_special(unsigned char id, unsigned long v)
{
	ipipe_trace_special(id, v);
	return 0;
}

static inline int xntrace_special_u64(unsigned char id,
					  unsigned long long v)
{
	ipipe_trace_special(id, (unsigned long)(v >> 32));
	ipipe_trace_special(id, (unsigned long)(v & 0xFFFFFFFF));
	return 0;
}

static inline int xntrace_pid(pid_t pid, short prio)
{
	ipipe_trace_pid(pid, prio);
	return 0;
}

static inline int xntrace_tick(unsigned long delay_tsc)
{
	ipipe_trace_event(0, delay_tsc);
	return 0;
}

static inline int xntrace_panic_freeze(void)
{
	ipipe_trace_panic_freeze();
	return 0;
}

static inline int xntrace_panic_dump(void)
{
	ipipe_trace_panic_dump();
	return 0;
}

#else /* !__KERNEL__ */

#include <asm/xenomai/syscall.h>

static inline int xntrace_max_begin(unsigned long v)
{
	return XENOMAI_SYSCALL2(sc_nucleus_trace, __xntrace_op_max_begin, v);
}

static inline int xntrace_max_end(unsigned long v)
{
	return XENOMAI_SYSCALL2(sc_nucleus_trace, __xntrace_op_max_end, v);
}

static inline int xntrace_max_reset(void)
{
	return XENOMAI_SYSCALL1(sc_nucleus_trace, __xntrace_op_max_reset);
}

static inline int xntrace_user_start(void)
{
	return XENOMAI_SYSCALL1(sc_nucleus_trace, __xntrace_op_user_start);
}

static inline int xntrace_user_stop(unsigned long v)
{
	return XENOMAI_SYSCALL2(sc_nucleus_trace, __xntrace_op_user_stop, v);
}

static inline int xntrace_user_freeze(unsigned long v, int once)
{
	return XENOMAI_SYSCALL3(sc_nucleus_trace, __xntrace_op_user_freeze,
				v, once);
}

static inline int xntrace_special(unsigned char id, unsigned long v)
{
	return XENOMAI_SYSCALL3(sc_nucleus_trace, __xntrace_op_special, id, v);
}

static inline int xntrace_special_u64(unsigned char id, unsigned long long v)
{
	return XENOMAI_SYSCALL4(sc_nucleus_trace, __xntrace_op_special_u64, id,
				(unsigned long)(v >> 32),
				(unsigned long)(v & 0xFFFFFFFF));
}

#endif /* !__KERNEL__ */

#endif /* !_XENO_NUCLEUS_TRACE_H */

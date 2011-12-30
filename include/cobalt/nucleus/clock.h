/**
 * @file
 * @note Copyright (C) 2006,2007 Philippe Gerum <rpm@xenomai.org>.
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
 * \ingroup clock
 */

#ifndef _XENO_NUCLEUS_CLOCK_H
#define _XENO_NUCLEUS_CLOCK_H

/*! \addtogroup clock
 *@{*/

#include <nucleus/queue.h>

#ifdef __KERNEL__

#include <nucleus/vfile.h>

#define XNTBLCK  0x00000001	/* Time base is locked. */

struct xnclock {
	xnticks_t wallclock_offset;
	unsigned long status;
#ifdef CONFIG_XENO_OPT_STATS
	struct xnvfile_snapshot vfile;
	struct xnvfile_rev_tag revtag;
	struct xnqueue timerq;
#endif /* CONFIG_XENO_OPT_STATS */
};

extern struct xnclock nkclock;

#ifdef __cplusplus
extern "C" {
#endif

static inline xnticks_t xnclock_get_offset(void)
{
	return nkclock.wallclock_offset;
}

static inline xnticks_t xnclock_read_monotonic(void)
{
	return xnarch_get_cpu_time();
}

static inline xnticks_t xnclock_read(void)
{
	/*
	 * Return an adjusted value of the monotonic time with the
	 * translated system wallclock offset.
	 */
	return xnclock_read_monotonic() + xnclock_get_offset();
}

static inline xnticks_t xnclock_read_raw(void)
{
	return xnarch_get_cpu_tsc();
}

void xnclock_adjust(xnsticks_t delta);

#ifdef __cplusplus
}
#endif

void xnclock_init_proc(void);

void xnclock_cleanup_proc(void);

#endif /* __KERNEL__ */

/*@}*/

#endif /* !_XENO_NUCLEUS_CLOCK_H */

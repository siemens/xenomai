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
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */
#ifndef _COBALT_UAPI_TIME_H
#define _COBALT_UAPI_TIME_H

#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW  4
#endif

/*
 * Additional clock ids we manage are supposed not to collide with any
 * of the POSIX and Linux kernel definitions so that no ambiguities
 * arise when porting applications in both directions.
 *
 * The Cobalt API reserves the first 32 extended clock codes. for
 * dynamically registered clocks. Everything from
 * __COBALT_CLOCK_CODE(32) onward can be reserved statically for
 * whatever purpose.
 */
#define COBALT_MAX_EXTCLOCKS  32
#define __COBALT_CLOCK_CODE(num)  ((clockid_t)((1 << 16)|num))
#define __COBALT_CLOCK_INDEX(id)  ((int)(id) & ~(1 << 16))
#define __COBALT_CLOCK_EXT_P(id)  ((int)(id) & (1 << 16))

#define CLOCK_HOST_REALTIME  __COBALT_CLOCK_CODE(42)

#endif /* !_COBALT_UAPI_TIME_H */

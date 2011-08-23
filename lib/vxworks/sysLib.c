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

#include "tickLib.h"
#include <copperplate/lock.h>
#include <vxworks/errnoLib.h>
#include <vxworks/sysLib.h>

int sysClkRateGet(void)
{
	unsigned int ns_per_tick;
	struct service svc;

	COPPERPLATE_PROTECT(svc);
	ns_per_tick = clockobj_get_period(&wind_clock);
	COPPERPLATE_UNPROTECT(svc);

	return 1000000000 / ns_per_tick;
}

STATUS sysClkRateSet(int hz)
{
	struct service svc;
	int ret;

	/*
	 * This is BSP level stuff, so we don't set errno upon error,
	 * but only return the ERROR status.
	 */
	if (hz <= 0)
		return ERROR;

	COPPERPLATE_PROTECT(svc);
	ret = clockobj_set_period(&wind_clock, 1000000000 / hz);
	COPPERPLATE_UNPROTECT(svc);

	return ret ? ERROR : OK;
}

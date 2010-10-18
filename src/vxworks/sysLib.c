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
#include <vxworks/errnoLib.h>
#include <vxworks/sysLib.h>

int sysClkRateGet(void)
{
	unsigned int ns_per_tick = clockobj_get_period(&wind_clock);
	return 1000000000 / ns_per_tick;
}

STATUS sysClkRateSet(int hz)
{
	int ret;

	/*
	 * This is BSP level stuff, so we don't set errno upon error,
	 * but only return the ERROR status.
	 */
	if (hz <= 0)
		return ERROR;

	ret = clockobj_set_period(&wind_clock, 1000000000 / hz);
	if (ret)
		return ERROR;

	return OK;
}

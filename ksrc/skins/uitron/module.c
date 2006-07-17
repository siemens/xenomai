/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
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
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "uitron/task.h"
#include "uitron/sem.h"
#include "uitron/flag.h"
#include "uitron/mbx.h"

MODULE_DESCRIPTION("uITRON interface");
MODULE_AUTHOR("rpm@xenomai.org");
MODULE_LICENSE("GPL");

static xnpod_t pod;

static void uitron_shutdown(int xtype)
{
	uimbx_cleanup();
	uiflag_cleanup();
	uisem_cleanup();
	uitask_cleanup();

	xnpod_shutdown(xtype);
}

int SKIN_INIT(uitron)
{
	int err;

#if CONFIG_XENO_OPT_TIMING_PERIOD == 0
	nktickdef = 1000000;	/* Defaults to 1ms. */
#endif

	err = xnpod_init(&pod, uITRON_MIN_PRI, uITRON_MAX_PRI, 0);

	if (err != 0)
		return err;

	uitask_init();
	uisem_init();
	uiflag_init();
	uimbx_init();

	xnprintf("starting uITRON services.\n");

	return 0;
}

void SKIN_EXIT(uitron)
{
	xnprintf("stopping uITRON services.\n");
	uitron_shutdown(XNPOD_NORMAL_EXIT);
}

module_init(__uitron_skin_init);
module_exit(__uitron_skin_exit);

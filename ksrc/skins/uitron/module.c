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

static u_long tick_arg = CONFIG_XENO_OPT_UITRON_PERIOD;
module_param_named(tick_arg, tick_arg, ulong, 0444);
MODULE_PARM_DESC(tick_arg, "Fixed clock tick value (us)");

xntbase_t *uitbase;

static void uitron_shutdown(int xtype)
{
	uimbx_cleanup();
	uiflag_cleanup();
	uisem_cleanup();
	uitask_cleanup();

	xntbase_free(uitbase);
	xncore_detach(xtype);
}

int SKIN_INIT(uitron)
{
	int err;

	err = xncore_attach(uITRON_MIN_PRI, uITRON_MAX_PRI);

	if (err != 0)
		return err;

	err = xntbase_alloc("uitron", tick_arg * 1000, &uitbase);

	if (err != 0) {
		xnlogerr("uITRON skin init failed, code %d.\n", err);
		xncore_detach(err);
		return err;
	}

	xntbase_start(uitbase);

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

/*
 * Copyright (C) 2007 Philippe Gerum <rpm@xenomai.org>.
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

#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <uitron/uitron.h>
#include <asm-generic/xenomai/bind.h>

int __uitron_muxid = -1;

static __constructor__ void __init_xeno_interface(void)
{
	T_CTSK pk_ctsk;
	ER err;

	__uitron_muxid = xeno_bind_skin(uITRON_SKIN_MAGIC, "uitron", "xeno_uitron");
	__uitron_muxid = __xn_mux_shifted_id(__uitron_muxid);

	/* Shadow the main thread. */
	pk_ctsk.stksz = 0;
	pk_ctsk.itskpri = 0;	/* non-RT shadow. */
	err = shd_tsk(1, &pk_ctsk);

	if (err) {
		fprintf(stderr, "Xenomai uITRON skin init: shd_tsk() failed, status %d", err);
		exit(EXIT_FAILURE);
	}
}

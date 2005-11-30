/*
 * Copyright (C) 2005 Philippe Gerum.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 * USA; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/linkage.h>
#include <asm/blackfin.h>
#include <asm/entry.h>

/*
 * Switch context routine for kernel-based Xenomai threads.
 *
 * r0=out_kspp, r1=in_kspp
 */
ENTRY(rthal_switch_context)

	SAVE_ALL_SYS
	p0 = r0;
	p1 = r1;

	[p0] = sp;
	sp = [p1];

	RESTORE_ALL_SYS
	rts;

/*
 * Copyright (C) 2006 Philippe Gerum <rpm@xenomai.org>.
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

#include <psos+/psos.h>

extern int __psos_muxid;

u_long q_create(char name[4], u_long maxnum, u_long flags, u_long *qid_r)
{
	return XENOMAI_SKINCALL4(__psos_muxid, __psos_q_create,
				 name, maxnum, flags, qid_r);
}

u_long q_delete(u_long qid)
{
	return XENOMAI_SKINCALL1(__psos_muxid, __psos_q_delete, qid);
}

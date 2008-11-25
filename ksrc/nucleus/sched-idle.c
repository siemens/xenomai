/*!\file sched-idle.c
 * \author Philippe Gerum
 * \brief Idle scheduling class implementation (i.e. Linux placeholder).
 *
 * Copyright (C) 2008 Philippe Gerum <rpm@xenomai.org>.
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
 * \ingroup sched
 */

#include <nucleus/sched.h>

static struct xnthread *xnsched_idle_pick(struct xnsched *sched)
{
	return &sched->rootcb;
}

struct xnsched_class xnsched_class_idle = {

#ifdef __XENO_SIM__
	.sched_init	=	NULL,
	.sched_enqueue	=	NULL,
	.sched_dequeue	=	NULL,
	.sched_requeue	=	NULL,
	.sched_pick	=	NULL,
	.sched_tick	=	NULL,
	.sched_rotate	=	NULL,
	.next		=	NULL,
#endif
	.sched_pick	=	xnsched_idle_pick,
	.weight		=	XNSCHED_CLASS_WEIGHT(0),
	.name		=	"idle"
};

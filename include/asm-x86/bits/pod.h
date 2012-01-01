/*
 * Copyright (C) 2012 Philippe Gerum <rpm@xenomai.org>.
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
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */
#ifndef _XENO_ASM_X86_BITS_POD_H
#define _XENO_ASM_X86_BITS_POD_H

#ifdef __i386__
#include "pod_32.h"
#else
#include "pod_64.h"
#endif

static inline int xnarch_escalate(void)
{
	if (ipipe_root_p) {
		ipipe_raise_irq(rthal_archdata.escalate_virq);
		return 1;
	}

	return 0;
}

#endif /* !_XENO_ASM_X86_BITS_POD_H */

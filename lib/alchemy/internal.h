/*
 * Copyright (C) 2011 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _ALCHEMY_INTERNAL_H
#define _ALCHEMY_INTERNAL_H

#include <copperplate/clockobj.h>

struct alchemy_namegen {
	const char *prefix;
	int length;
	int serial;
};

char *__alchemy_build_name(char *buf, const char *name,
			   struct alchemy_namegen *ngen);

ticks_t __alchemy_rel2abs_timeout(ticks_t timeout);

#endif /* !_ALCHEMY_INTERNAL_H */

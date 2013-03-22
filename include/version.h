/*
 * Copyright (C) 2001-2013 Philippe Gerum <rpm@xenomai.org>.
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
 */

#ifndef _XENOMAI_VERSION_H
#define _XENOMAI_VERSION_H

#ifndef __KERNEL__
#include <xeno_config.h>
#define __stringify_1(x...)	#x
#define __stringify(x...)	__stringify_1(x)
#endif

#define XENO_VERSION(maj, min, rev)  (((maj)<<16)|((min)<<8)|(rev))

#define XENO_VERSION_CODE  XENO_VERSION(CONFIG_XENO_VERSION_MAJOR, \
					    CONFIG_XENO_VERSION_MINOR, \
					    CONFIG_XENO_REVISION_LEVEL)

#define XENO_VERSION_STRING	__stringify(CONFIG_XENO_VERSION_MAJOR) "." \
			        __stringify(CONFIG_XENO_VERSION_MINOR) "." \
				__stringify(CONFIG_XENO_REVISION_LEVEL)

#endif /* _XENOMAI_VERSION_H */

/*
 * Copyright (C) 2006 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _XENO_NUCLEUS_ASSERT_H
#define _XENO_NUCLEUS_ASSERT_H

#include <nucleus/compiler.h>

#define XENO_ASSERT(subsystem,cond,action)  do { \
if (unlikely(CONFIG_XENO_OPT_DEBUG_##subsystem > 0 && !(cond))) \
	do { action; } while(0); \
} while(0)

#define XENO_BUGON(subsystem,cond)  \
    XENO_ASSERT(subsystem,cond,xnpod_fatal("assertion failed at %s:%d",__FILE__,__LINE__))

#endif /* !_XENO_NUCLEUS_ASSERT_H */

/*
 * Copyright (C) 2014 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _COBALT_X86_ASM_SYSCALL32_TABLE_H
#define _COBALT_X86_ASM_SYSCALL32_TABLE_H

/*
 * CAUTION: This file is read verbatim into the main syscall
 * table. Only preprocessor stuff and syscall entries here.
 */

#ifdef CONFIG_X86_X32

/*
 * When x32 support is enabled, we need thunks for dealing with
 * 32<->64 argument conversion. An additional entry for each
 * __COBALT_CALL_X32 syscall is generated into the table, at a
 * position equal to the original syscall number + __COBALT_X32_BASE
 * as defined in asm/xenomai/syscall32.h.
 */

#define __sysx32__(__name)		\
	((cobalt_syshand)(cobalt32x_ ## __name))

#define __COBALT_CALL_X32(__name)	\
	[sc_cobalt_ ## __name + __COBALT_X32_BASE] = __sysx32__(__name),

#endif

#endif /* !_COBALT_X86_ASM_SYSCALL32_TABLE_H */

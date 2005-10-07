/**
 * @file
 * This file is part of the Xenomai project.
 *
 * @note Copyright (C) 2004 Philippe Gerum <rpm@xenomai.org> 
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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

#ifndef _XENO_ASM_PPC_UART_H
#define _XENO_ASM_PPC_UART_H

#include <linux/config.h>

#if defined(CONFIG_SANDPOINT)

#define TTYS0  { 0xfe0003f8, 4 }
#define TTYS1  { 0xfe0002f8, 3 }
#else
#error "UART configuration is undefined for this PowerPC platform"
#endif

#endif /* !_XENO_ASM_PPC_UART_H */

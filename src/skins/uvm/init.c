/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <asm-uvm/syscall.h>

int __uvm_muxid = -1;

xnsysinfo_t __uvm_info;

static __attribute__ ((constructor))
void __init_uvm_interface(void)
{
	__uvm_muxid = xeno_bind_skin(UVM_SKIN_MAGIC, "UVM", "xeno_uvm");

	XENOMAI_SYSCALL2(__xn_sys_info, __uvm_muxid, &__uvm_info);
}

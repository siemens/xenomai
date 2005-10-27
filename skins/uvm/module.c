/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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

#include <nucleus/pod.h>
#include <nucleus/core.h>
#include <uvm/syscall.h>

MODULE_DESCRIPTION("UVM skin");
MODULE_AUTHOR("rpm@xenomai.org");
MODULE_LICENSE("GPL");

int __xeno_skin_init(void)
{
    int err = xncore_attach();

    if (err)
	return err;

    err = __uvm_syscall_init();

    if (err)
        xnpod_shutdown(err);    
    else
	xnprintf("starting UVM services.\n");

    return err;
}

void __xeno_skin_exit(void)
{
    xnprintf("stopping UVM services.\n");
    __uvm_syscall_cleanup();
    xncore_detach();
}

module_init(__xeno_skin_init);
module_exit(__xeno_skin_exit);

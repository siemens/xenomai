/**
 *
 * @note Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org> 
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

#include <nucleus/pod.h>
#include <nucleus/heap.h>
#include <nucleus/shadow.h>
#include <rtai/syscall.h>

static int __rtai_muxid;

static void __shadow_delete_hook (xnthread_t *thread)

{
    if (xnthread_get_magic(thread) == RTAI_SKIN_MAGIC &&
	testbits(thread->status,XNSHADOW))
	xnshadow_unmap(thread);
}

static xnsysent_t __systab[] = {
    [0] = { NULL, 0  },
};

int __rtai_syscall_init (void)

{
    __rtai_muxid =
	xnshadow_register_interface("rtai",
				    RTAI_SKIN_MAGIC,
				    sizeof(__systab) / sizeof(__systab[0]),
				    __systab,
				    NULL);
    if (__rtai_muxid < 0)
	return -ENOSYS;

    xnpod_add_hook(XNHOOK_THREAD_DELETE,&__shadow_delete_hook);
    
    return 0;
}

void __rtai_syscall_cleanup (void)

{
    xnpod_remove_hook(XNHOOK_THREAD_DELETE,&__shadow_delete_hook);
    xnshadow_unregister_interface(__rtai_muxid);
}

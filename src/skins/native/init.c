/*
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
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

#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <pthread.h>
#include <native/syscall.h>
#include <native/task.h>

pthread_key_t __native_tskey;

int __native_muxid = -1;

static void __flush_tsd (void *tsd)

{
    /* Free the task descriptor allocated by rt_task_self(). */
    free(tsd);
}

static __attribute__((constructor)) void __init_xeno_interface(void)

{
    __native_muxid = xeno_user_skin_init(XENO_SKIN_MAGIC,"native","xeno_native");
    
    /* Allocate a TSD key for indexing self task pointers. */

    if (pthread_key_create(&__native_tskey,&__flush_tsd) != 0)
        {
        fprintf(stderr,"Xenomai: failed to allocate new TSD key?!\n");
        exit(1);
        }    
}

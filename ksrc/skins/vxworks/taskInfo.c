/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
 * Copyright (C) 2003 Philippe Gerum <rpm@xenomai.org>.
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
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "xenomai/vxworks/defs.h"




/* TODO: check (in the doc or implementation) what should happen if task_id is
   zero */
char * taskName ( TASK_ID task_id )
{
    wind_task_t * task;
    
    task = wind_h2obj_active(task_id, WIND_TASK_MAGIC,wind_task_t);

    /* It is useless to lock the access to task here, because if the task
       is deleted, the returned pointer will be invalid anyway. */
    
    if(!task)
        return NULL;
    else
        return task->name;
}


int taskIdDefault ( TASK_ID task_id )
{
    static int value = 0;

    if(task_id)
        value = task_id;

    return value;
}
    

BOOL taskIsReady ( TASK_ID task_id )
{
    wind_task_t * task;
    
    check_OBJ_ID_ERROR(task_id,wind_task_t,task,WIND_TASK_MAGIC,return 0 );

    return testbits(xnthread_status_flags(&task->threadbase),XNREADY);
    /* TODO: maybe we should check if task is the current task (it depends
       whether taskIsReady(taskIdSelf()) should return true or false, which is
       not documented) */
}


BOOL taskIsSuspended ( TASK_ID task_id )
{
    wind_task_t * task;

    check_OBJ_ID_ERROR(task_id,wind_task_t,task,WIND_TASK_MAGIC,return 0 );

    return testbits(xnthread_status_flags(&task->threadbase),XNSUSP);
}

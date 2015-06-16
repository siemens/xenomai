/*
 * Copyright (C) 2015 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _VXWORKS_TASKHOOKLIB_H
#define _VXWORKS_TASKHOOKLIB_H

#include <vxworks/taskLib.h>

struct wind_task;

struct wind_task_hook {
	void (*handler)(TASK_ID tid);
	struct pvholder next;
};

extern struct pvlistobj wind_create_hooks;

extern struct pvlistobj wind_delete_hooks;

void wind_run_hooks(struct pvlistobj *list,
		    struct wind_task *task);

#endif /* _VXWORKS_TASKHOOKLIB_H */

/*
 * Copyright (C) 2013 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _MERCURY_COPPERPLATE_SIGNAL_H
#define _MERCURY_COPPERPLATE_SIGNAL_H

#include <signal.h>

#ifndef sigev_notify_thread_id
#define sigev_notify_thread_id	 _sigev_un._tid
#endif

#define SIGNOTIFY	(SIGRTMIN + 8) /* Internal notification */
#define SIGRELS		(SIGRTMIN + 9) /* Syscall abort */

#endif /* _MERCURY_COPPERPLATE_SIGNAL_H */
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
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <copperplate/heapobj.h>
#include <copperplate/threadobj.h>
#include "sysregfs.h"
#include "../internal.h"

#ifdef CONFIG_XENO_PSHARED

static int retrieve_task_state(pid_t pid)
{
	char trash[BUFSIZ], state = '?', path[32];
	FILE *fp;
	int ret;

	/*
	 * Try to figure out the state in kernel context of a Mercury
	 * task which does not wait on a copperplate service. If this
	 * task is not runnable, then display 'X', which is
	 * reminiscent of a Cobalt task running out of real-time mode.
	 * Otherwise, show this task as runnable.
	 */
	snprintf(path, sizeof(path), "/proc/%d/stat", pid);
	fp = fopen(path, "r");
	if (fp) {
		ret = fscanf(fp, "%[0-9] (%[^) ]) %c", trash, trash, &state);
		fclose(fp);
		if (ret == 3 && state != 'R')
			state = 'X';
	}

	return state;
}

char *format_thread_status(const struct thread_data *p, char *buf, size_t len)
{
	char *wp = buf;

	if (len < 4)
		return NULL;

	if (p->status & __THREAD_S_TIMEDWAIT)
		*wp++ = 'w';
	else if (p->status & __THREAD_S_WAIT)
		*wp++ = 'W';
	else if (p->status & __THREAD_S_DELAYED)
		*wp++ = 'D';
	else if (p->status & __THREAD_S_STARTED)
		*wp++ = retrieve_task_state(p->pid);
	else
		*wp++ = 'U';

	if (p->schedlock > 0)
		*wp++ = 'l';

	if (p->policy == SCHED_RR)
		*wp++ = 'r';

	*wp = '\0';

	return buf;
}

#endif /* CONFIG_XENO_PSHARED */

struct sysreg_fsdir sysreg_dirs[] = {
	{
		.path = NULL,
	},
};

struct sysreg_fsfile sysreg_files[] = {
#ifdef CONFIG_XENO_PSHARED
	{
		.path = "/threads",
		.mode = O_RDONLY,
		.ops = {
			.open = open_threads,
			.release = fsobj_obstack_release,
			.read = fsobj_obstack_read
		},
	},
	{
		.path = "/heaps",
		.mode = O_RDONLY,
		.ops = {
			.open = open_heaps,
			.release = fsobj_obstack_release,
			.read = fsobj_obstack_read
		},
	},
#endif /* CONFIG_XENO_PSHARED */
	{
		.path = "/version",
		.mode = O_RDONLY,
		.ops = {
			.open = open_version,
			.release = fsobj_obstack_release,
			.read = fsobj_obstack_read
		},
	},
	{
		.path = NULL,
	}
};

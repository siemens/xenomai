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
#include <errno.h>
#include <cobalt/uapi/thread.h>
#include "sysregfs.h"

#ifdef CONFIG_XENO_PSHARED
/*
 * This is a blunt copy of what we do in kernel space to produce this
 * status in /proc/xenomai/sched/threads. There are additional states
 * for a thread compared to Mercury, introduced by the dual kernel
 * (such as relaxed mode, mode switch trap, and priority boost).
 */
char *format_thread_status(const struct thread_data *p, char *buf, size_t len)
{
	static const char labels[] = XNTHREAD_STATE_LABELS;
	unsigned long mask;
	int pos, c;
	char *wp;

	for (mask = p->status, pos = 0, wp = buf;
	     mask != 0 && wp - buf < len - 2;	/* 1-letter label + \0 */
	     mask >>= 1, pos++) {
		if ((mask & 1) == 0)
			continue;

		c = labels[pos];

		switch (1 << pos) {
		case XNROOT:
			c = 'R'; /* Always mark root as runnable. */
			break;
		case XNREADY:
			if (p->status & XNROOT)
				continue; /* Already reported on XNROOT. */
			break;
		case XNDELAY:
			/*
			 * Only report genuine delays here, not timed
			 * waits for resources.
			 */
			if (p->status & XNPEND)
				continue;
			break;
		case XNPEND:
			/* Report timed waits with lowercase symbol. */
			if (p->status & XNDELAY)
				c |= 0x20;
			break;
		default:
			if (c == '.')
				continue;
		}
		*wp++ = c;
	}

	*wp = '\0';

	return buf;
}

#else /* !CONFIG_XENO_PSHARED */

/*
 * If we have no session information, fallback to reading
 * /proc/xenomai.
 */

#define PROC_PULL_HANDLER(__name, __path)				\
int open_ ## __name(struct fsobj *fsobj, void *priv)			\
{									\
	return pull_proc_data("/proc/xenomai/" __path, priv);		\
}

/*
 * Cobalt-specific helper to pull the /proc vfile data provided by the
 * nucleus over a fuse-managed vfile.
 */
static int pull_proc_data(const char *path, struct fsobstack *o)
{
	int len;

	fsobstack_init(o);
	len = fsobstack_grow_file(o, path);
	fsobstack_finish(o);

	return len < 0 ? len : 0;
}

PROC_PULL_HANDLER(threads, "/sched/threads");
PROC_PULL_HANDLER(heaps, "/heap");

#endif /* !CONFIG_XENO_PSHARED */

struct sysreg_fsdir sysreg_dirs[] = {
	{
		.path = NULL,
	},
};

struct sysreg_fsfile sysreg_files[] = {
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

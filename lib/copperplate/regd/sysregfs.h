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

#ifndef _REGD_SYSREGFS_H
#define _REGD_SYSREGFS_H

#include <copperplate/clockobj.h>
#include <copperplate/registry-obstack.h>

struct sysreg_fsdir {
	const char *path;
};

struct sysreg_fsfile {
	const char *path;
	struct fsobj fsobj;
	int mode;
	struct registry_operations ops;
};

struct thread_data {
	char name[XNOBJECT_NAME_LEN];
	pid_t pid;
	int priority;
	int policy;
	int cpu;
	int schedlock;
	ticks_t timeout;
	unsigned long status;
};

extern struct sysreg_fsdir sysreg_dirs[];

extern struct sysreg_fsfile sysreg_files[];

int open_threads(struct fsobj *fsobj, void *priv);

int open_heaps(struct fsobj *fsobj, void *priv);

int open_version(struct fsobj *fsobj, void *priv);

char *format_thread_status(const struct thread_data *p,
			   char *buf, size_t len);

#endif /* !_REGD_SYSREGFS_H */

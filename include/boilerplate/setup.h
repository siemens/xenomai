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
#ifndef _BOILERPLATE_SETUP_H
#define _BOILERPLATE_SETUP_H

#include <boilerplate/list.h>
#include <sched.h>

struct base_setup_data {
	cpu_set_t cpu_affinity;
	int no_mlock;
	int no_sanity;
	int silent_mode;
	const char *arg0;
};

struct option;

struct skin_descriptor {
	const char *name;
	int (*init)(void);
	const struct option *options;
	int (*parse_option)(int optnum, const char *optarg);
	void (*help)(void);
	struct {
		int opt_start;
		int opt_end;
		struct pvholder next;
	} __reserved;
};

#define DECLARE_SKIN(__name, __priority)		\
static __attribute__ ((constructor(__priority)))	\
void __declare_ ## __name(void)				\
{							\
	__register_skin(&(__name));			\
}

#ifdef __cplusplus
extern "C" {
#endif

void __register_skin(struct skin_descriptor *p);
	
void xenomai_init(int *argcp, char *const **argvp);

extern pid_t __node_id;

extern struct base_setup_data __base_setup_data;

extern const char *xenomai_version_string;

#ifdef __cplusplus
}
#endif

#endif /* !_BOILERPLATE_SETUP_H */

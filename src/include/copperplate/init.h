/*
 * Copyright (C) 2008 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _COPPERPLATE_INIT_H
#define _COPPERPLATE_INIT_H

#include <xeno_config.h>
#include <sched.h>

struct coppernode {
	pid_t id;
	unsigned int tick_period;
	unsigned int mem_pool;
	char *registry_mountpt;
	const char *session_label;
	cpu_set_t cpu_affinity;
	/* No bitfield below, we have to take address of thoses. */
	int no_mlock;
	int no_registry;
	int reset_session;
};

extern struct coppernode __this_node;

#ifdef __cplusplus
extern "C" {
#endif

int copperplate_init(int argc, char *const argv[]);

pid_t copperplate_get_tid(void);

int copperplate_probe_node(unsigned int id);

void panic(const char *fmt, ...);

void warning(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* _COPPERPLATE_INIT_H */

/*
 * Copyright (C) 2011 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _COPPERPLATE_INTERNAL_H
#define _COPPERPLATE_INTERNAL_H

#include <sys/types.h>
#include <stdarg.h>
#include <pthread.h>
#include <sched.h>
#include <xeno_config.h>
#include <copperplate/list.h>

struct coppernode {
	unsigned int mem_pool;
	char *registry_mountpt;
	const char *session_label;
	cpu_set_t cpu_affinity;
	/* No bitfield below, we have to take address of thoses. */
	int no_mlock;
	int no_registry;
	int reset_session;
	int silent_mode;
};

extern pid_t __node_id;

extern struct coppernode __node_info;

extern struct timespec __init_date;

extern const char *dashes;

extern pthread_mutex_t __printlock;

struct threadobj;
struct error_frame;

#ifdef __cplusplus
extern "C" {
#endif

void __printout(struct threadobj *thobj,
		const char *header,
		const char *fmt, va_list ap);

void error_hook(struct error_frame *ef);

const char *symerror(int errnum);

void panic(const char *fmt, ...);

void warning(const char *fmt, ...);

pid_t copperplate_get_tid(void);

int copperplate_probe_node(unsigned int id);

int copperplate_create_thread(int prio,
			      void *(*start)(void *arg), void *arg,
			      size_t stacksize,
			      pthread_t *tid);

int copperplate_renice_thread(pthread_t tid, int prio);

#ifdef __cplusplus
}
#endif

#endif /* _COPPERPLATE_INTERNAL_H */

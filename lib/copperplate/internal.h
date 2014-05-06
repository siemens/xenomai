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
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>
#include <xeno_config.h>
#include <boilerplate/list.h>
#include <boilerplate/ancillaries.h>
#include <boilerplate/limits.h>
#include <copperplate/heapobj.h>

#define HOBJ_MINLOG2    3
#define HOBJ_MAXLOG2    22     /* Must hold pagemap::bcount objects */
#define HOBJ_NBUCKETS   (HOBJ_MAXLOG2 - HOBJ_MINLOG2 + 2)

/*
 * The struct below has to live in shared memory; no direct reference
 * to process local memory in there.
 */
struct shared_heap {
	char name[XNOBJECT_NAME_LEN];
	pthread_mutex_t lock;
	struct list extents;
	size_t extentsize;
	size_t hdrsize;
	size_t npages;
	size_t ubytes;
	size_t total;
	size_t maxcont;
	struct sysgroup_memspec memspec;
	struct {
		memoff_t freelist;
		int fcount;
	} buckets[HOBJ_NBUCKETS];
};

struct corethread_attributes {
	int prio;
	size_t stacksize;
	int detachstate;
	int (*prologue)(void *arg);
	void *(*run)(void *arg);
	void *arg;
	struct {
		int status;
		sem_t warm;
		sem_t *released;
	} __reserved;
};

extern pid_t __node_id;

#ifdef __cplusplus
extern "C" {
#endif

pid_t copperplate_get_tid(void);

int copperplate_kill_tid(pid_t tid, int sig);

int copperplate_probe_node(unsigned int id);

int copperplate_create_thread(struct corethread_attributes *cta,
			      pthread_t *tid);

int copperplate_renice_thread(pthread_t tid, int prio);

void copperplate_bootstrap_minimal(const char *arg0,
				   char *mountpt);

#ifdef __cplusplus
}
#endif

#endif /* _COPPERPLATE_INTERNAL_H */

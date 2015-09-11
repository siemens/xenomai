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
#include <xeno_config.h>
#include <boilerplate/list.h>
#include <boilerplate/ancillaries.h>
#include <boilerplate/limits.h>
#include <boilerplate/sched.h>
#include <boilerplate/setup.h>
#include <copperplate/heapobj.h>
#include <copperplate/tunables.h>

#ifdef CONFIG_XENO_REGISTRY
#define DEFAULT_REGISTRY_ROOT		CONFIG_XENO_REGISTRY_ROOT
#else
#define DEFAULT_REGISTRY_ROOT		NULL
#endif

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
	struct listobj extents;
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
	size_t stacksize;
	int detachstate;
	int policy;
	struct sched_param_ex param_ex;
	int (*prologue)(void *arg);
	void *(*run)(void *arg);
	void *arg;
	struct {
		int status;
		sem_t warm;
		sem_t *released;
	} __reserved;
};

#ifdef __cplusplus
extern "C" {
#endif

void copperplate_set_current_name(const char *name);

int copperplate_get_current_name(char *name, size_t maxlen);

int copperplate_kill_tid(pid_t tid, int sig);

int copperplate_probe_tid(pid_t tid);
  
int copperplate_create_thread(struct corethread_attributes *cta,
			      pthread_t *ptid);

int copperplate_renice_local_thread(pthread_t ptid, int policy,
				    const struct sched_param_ex *param_ex);

void copperplate_bootstrap_internal(const char *arg0,
				    char *mountpt, int regflags);

#ifdef __cplusplus
}
#endif

#endif /* _COPPERPLATE_INTERNAL_H */

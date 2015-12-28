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
#include <signal.h>
#include <malloc.h>
#include <sched.h>
#include <copperplate/heapobj.h>
#include <copperplate/threadobj.h>
#include <copperplate/clockobj.h>
#include <xenomai/version.h>
#include "sysregfs.h"
#include "../internal.h"

#ifdef CONFIG_XENO_PSHARED

/*
 * If --enable-pshared was given, we can access the main shared heap
 * to retrieve session-wide information.
 */

static char *format_time(ticks_t value, char *buf, size_t bufsz)
{
	unsigned long ms, us, ns;
	char *p = buf;
	ticks_t s;

	if (value == 0) {
		strcpy(buf, "-");
		return buf;
	}

	s = value / 1000000000ULL;
	ns = value % 1000000000ULL;
	us = ns / 1000;
	ms = us / 1000;
	us %= 1000;

	if (s)
		p += snprintf(p, bufsz, "%Lus", s);

	if (ms || (s && us))
		p += snprintf(p, bufsz - (p - buf), "%lums", ms);

	if (us)
		p += snprintf(p, bufsz - (p - buf), "%luus", us);

	return buf;
}

int open_threads(struct fsobj *fsobj, void *priv)
{
	struct thread_data *thread_data, *p;
	struct sysgroup_memspec *obj, *tmp;
	char sbuf[64], pbuf[16], tbuf[64];
	struct threadobj_stat statbuf;
	struct fsobstack *o = priv;
	struct threadobj *thobj;
	const char *sched_class;
	int ret, count, len = 0;

	ret = heapobj_bind_session(__copperplate_setup_data.session_label);
	if (ret)
		return ret;

	fsobstack_init(o);

	sysgroup_lock();
	count = sysgroup_count(thread);
	sysgroup_unlock();

	if (count == 0)
		goto out;

	/*
	 * We don't want to hold the sysgroup lock for too long, since
	 * it could be contended by a real-time task. So we pull all
	 * the per-thread data we need into a local array, before
	 * printing out its contents after we dropped the lock.
	 */
	thread_data = p = malloc(sizeof(*p) * count);
	if (thread_data == NULL) {
		len = -ENOMEM;
		goto out;
	}

	sysgroup_lock();

	for_each_sysgroup(obj, tmp, thread) {
		if (p - thread_data >= count)
			break;
		thobj = container_of(obj, struct threadobj, memspec);
		ret = threadobj_lock(thobj);
		if (ret) {
			sysgroup_remove(thread, obj);
			continue;
		}
		namecpy(p->name, thobj->name);
		p->name[sizeof(p->name) - 1] = '\0';
		p->pid = thobj->pid;
		p->priority = threadobj_get_priority(thobj);
		p->policy = threadobj_get_policy(thobj);
		ret = threadobj_stat(thobj, &statbuf);
		threadobj_unlock(thobj);
		if (ret)
			p->cpu = -1;
		else {
			p->status = statbuf.status;
			p->cpu = statbuf.cpu;
			p->timeout = statbuf.timeout;
			p->schedlock = statbuf.schedlock;
		}
		p++;
	}

	sysgroup_unlock();

	count = p - thread_data;
	if (count == 0)
		goto out_free;

	len = fsobstack_grow_format(o, "%-3s  %-6s %-5s  %-8s %-8s  %-10s %s\n",
				    "CPU", "PID", "CLASS", "PRI", "TIMEOUT",
				    "STAT", "NAME");

	for (p = thread_data; count > 0; count--) {
		if (kill(p->pid, 0))
			continue;
		snprintf(pbuf, sizeof(pbuf), "%3d", p->priority);
		if (p->cpu < 0) {
			strcpy(tbuf, "????");
			strcpy(sbuf, "??");
		} else {
			format_time(p->timeout, tbuf, sizeof(tbuf));
			format_thread_status(p, sbuf, sizeof(sbuf));
		}
		switch (p->policy) {
		case SCHED_FIFO:
			sched_class = "fifo";
			break;
		case SCHED_RR:
			sched_class = "rr";
			break;
#ifdef SCHED_SPORADIC
		case SCHED_SPORADIC:
			sched_class = "pss";
			break;
#endif
#ifdef SCHED_TP
		case SCHED_TP:
			sched_class = "tp";
			break;
#endif
#ifdef SCHED_QUOTA
		case SCHED_QUOTA:
			sched_class = "quota";
			break;
#endif
#ifdef SCHED_QUOTA
		case SCHED_WEAK:
			sched_class = "weak";
			break;
#endif
		default:
			sched_class = "other";
			break;
		}
		len += fsobstack_grow_format(o,
					     "%3d  %-6d %-5s  %-8s %-8s  %-10s %s\n",
					     p->cpu, p->pid, sched_class, pbuf,
					     tbuf, sbuf, p->name);
		p++;
	}

out_free:
	free(thread_data);
out:
	heapobj_unbind_session();

	fsobstack_finish(o);

	return len < 0 ? len : 0;
}

struct heap_data {
	char name[XNOBJECT_NAME_LEN];
	size_t total;
	size_t used;
};

int open_heaps(struct fsobj *fsobj, void *priv)
{
	struct sysgroup_memspec *obj, *tmp;
	struct heap_data *heap_data, *p;
	struct fsobstack *o = priv;
	struct shared_heap *heap;
	int ret, count, len = 0;

	ret = heapobj_bind_session(__copperplate_setup_data.session_label);
	if (ret)
		return ret;

	fsobstack_init(o);

	sysgroup_lock();
	count = sysgroup_count(heap);
	sysgroup_unlock();

	if (count == 0)
		goto out;

	heap_data = p = malloc(sizeof(*p) * count);
	if (heap_data == NULL) {
		len = -ENOMEM;
		goto out;
	}

	sysgroup_lock();

	/*
	 * A heap we find there cannot totally vanish until we drop
	 * the group lock, so there is no point in acquiring each heap
	 * lock individually for reading the slot.
	 */
	for_each_sysgroup(obj, tmp, heap) {
		if (p - heap_data >= count)
			break;
		heap = container_of(obj, struct shared_heap, memspec);
		namecpy(p->name, heap->name);
		p->used = heap->ubytes;
		p->total = heap->total;
		p++;
	}

	sysgroup_unlock();

	count = p - heap_data;
	if (count == 0)
		goto out_free;

	len = fsobstack_grow_format(o, "%9s %9s  %s\n",
				    "TOTAL", "USED", "NAME");

	for (p = heap_data; count > 0; count--) {
		len += fsobstack_grow_format(o, "%9Zu %9Zu  %s\n",
					     p->total, p->used, p->name);
		p++;
	}

out_free:
	free(heap_data);
out:
	heapobj_unbind_session();

	fsobstack_finish(o);

	return len < 0 ? len : 0;
}

#endif /* CONFIG_XENO_PSHARED */

int open_version(struct fsobj *fsobj, void *priv)
{
	struct fsobstack *o = priv;

	fsobstack_init(o);
	fsobstack_grow_format(o, "%s\n", XENO_VERSION_STRING);
	fsobstack_finish(o);

	return 0;
}

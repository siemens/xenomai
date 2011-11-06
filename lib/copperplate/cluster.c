/*
 * Copyright (C) 2010 Philippe Gerum <rpm@xenomai.org>.
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

 /*
 * This file implements object clusters, to group various related
 * runtime objects in named tables. Objects within clusters are
 * indexed on a string label. Depending on whether shared
 * multi-processing mode is enabled, clusters may be persistent in the
 * main heap.
 *
 * In its simplest form - when shared multi-processing is disabled -,
 * a cluster is basically a private hash table only known from the
 * process who created it.
 *
 * When shared multi-processing mode is enabled, a cluster is a shared
 * hash table indexed on a unique name within the main catalog.
 * Therefore, all objects referred to by the cluster should be laid
 * into the main heap as well.  Multiple processes attached to the
 * same copperplate session do share the same main heap. Therefore,
 * they may share objects by providing:
 *
 * - the name of the cluster.
 * - the name of the object to retrieve from the cluster.
 *
 * Having objects shared between processes introduces the requirement
 * to deal with stale objects, created by processes that don't exist
 * anymore when a lookup is performed on a cluster by another
 * process. We deal with this issue as simply as we can, as follows:
 *
 * - each object referenced to by a cluster bears a "creator node"
 * identifier. This is basically the system-wide linux TID of the
 * process owning the thread which has initially added the object to
 * the cluster (i.e. getpid() as returned from the NPTL).
 *
 * - upon a lookup operation in the cluster which matches an object in
 * the table, the process who introduced the object is probed for
 * existence. If the process is gone, we silently drop the reference
 * to the orphaned object from the cluster, and return a failed lookup
 * status. Otherwise, the lookup succeeds.
 *
 * - when an attempt is made to index an object into cluster, any
 * conflicting object which bears the same name is checked for
 * staleness as described for the lookup operation. However, the
 * insertion succeeds after the reference to a conflicting stale
 * object was silently discarded.
 *
 * The test for existence based on the linux TID may return spurious
 * "true" results in case an object was created by a long gone
 * process, whose TID was eventually reused for a newer process,
 * before the process who initialized the main heap has exited. In
 * theory, this situation may happen; in practice, 1) the TID
 * generator has to wrap around fully before this happens, 2) multiple
 * processes sharing objects via a cluster are normally co-operating
 * to implement a global functionality. In the event of a process
 * exit, it is likely that the whole application system should be
 * reinited, thus the main (session) heap would be reset, which would
 * in turn clear the issue.
 *
 * In the worst case, using a stale object would never cause bad
 * memory references, since a clustered object - and all the memory
 * references it does via its members - must be laid into the main
 * heap, which is persistent until the last process attached to it
 * leaves the session.
 *
 * This stale object detection is essentially a sanity mechanism to
 * cleanup obviously wrong references from clusters after some process
 * died unexpectedly. Under normal circumstances, for an orderly exit,
 * a process should remove all references to objects it has created
 * from existing clusters, before eventually freeing those objects.
 *
 * In addition to the basic cluster object, the synchronizing cluster
 * (struct syncluster) provides support for waiting for a given object
 * to appear in the dictionary.
 */

#include <errno.h>
#include <string.h>
#include "copperplate/init.h"
#include "copperplate/heapobj.h"
#include "copperplate/cluster.h"
#include "copperplate/syncobj.h"
#include "copperplate/threadobj.h"
#include "copperplate/debug.h"

struct syncluster_wait_struct {
	const char *name;
};

#ifdef CONFIG_XENO_PSHARED

int cluster_init(struct cluster *c, const char *name)
{
	struct dictionary *d;
	struct hashobj *hobj;
	int ret = 0;

	/*
	 * NOTE: it does not make sense to destroy a shared cluster
	 * since other processes from the same session will likely
	 * have references on it, so there is no cluster_destroy()
	 * routine on purpose. When all processes from the session are
	 * gone, the shared heap is cleared next time the application
	 * boots, so there is really no use of deleting shared
	 * clusters.
	 */
redo:
	hobj = hash_search(&main_catalog, name);
	if (hobj) {
		d = container_of(hobj, struct dictionary, hobj);
		goto out;
	}

	d = xnmalloc(sizeof(*d));
	if (d == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	hash_init(&d->table);
	ret = hash_enter(&main_catalog, name, &d->hobj);
	if (ret == -EEXIST) {
		/*
		 * Someone seems to have slipped in, creating the
		 * cluster right after we failed retrieving it: retry
		 * the whole process.
		 */
		xnfree(d);
		goto redo;
	}
out:
	c->d = d;

	return __bt(ret);
}

static int cluster_probe(struct hashobj *hobj)
{
	struct clusterobj *cobj;

	cobj = container_of(hobj, struct clusterobj, hobj);
	if (cobj->cnode == __this_node.id)
		return 1; /* Trivial check: is it ours? */

	return copperplate_probe_node(cobj->cnode);
}

int cluster_addobj(struct cluster *c, const char *name,
		   struct clusterobj *cobj)
{
	cobj->cnode = __this_node.id;
	/*
	 * Add object to cluster and probe conflicting entries for
	 * owner node existence, overwriting dead instances on the
	 * fly.
	 */
	return __bt(hash_enter_probe(&c->d->table, name,
				     &cobj->hobj, cluster_probe));
}

int cluster_addobj_dup(struct cluster *c, const char *name,
		       struct clusterobj *cobj)
{
	cobj->cnode = __this_node.id;
	/*
	 * Same as cluster_addobj(), but allows for duplicate keys in
	 * live objects.
	 */
	return __bt(hash_enter_probe_dup(&c->d->table, name,
					 &cobj->hobj, cluster_probe));
}

int cluster_delobj(struct cluster *c, struct clusterobj *cobj)
{
	return __bt(hash_remove(&c->d->table, &cobj->hobj));
}

struct clusterobj *cluster_findobj(struct cluster *c, const char *name)
{
	struct hashobj *hobj;

	/*
	 * Search for object entry and probe for owner node existence,
	 * discarding dead instances on the fly.
	 */
	hobj = hash_search_probe(&c->d->table, name, cluster_probe);
	if (hobj == NULL)
		return NULL;

	return container_of(hobj, struct clusterobj, hobj);
}

int syncluster_init(struct syncluster *sc, const char *name)
{
	int ret;

	ret = __bt(cluster_init(&sc->c, name));
	if (ret)
		return ret;

	sc->sobj = xnmalloc(sizeof(*sc->sobj));
	if (sc->sobj == NULL)
		return -ENOMEM;

	syncobj_init(sc->sobj, SYNCOBJ_FIFO, fnref_null);

	return 0;
}

int syncluster_addobj(struct syncluster *sc, const char *name,
		      struct clusterobj *cobj)
{
	struct syncluster_wait_struct *wait;
	struct threadobj *thobj, *tmp;
	struct syncstate syns;
	int ret;

	if (syncobj_lock(sc->sobj, &syns))
		return __bt(-EINVAL);

	ret = __bt(cluster_addobj(&sc->c, name, cobj));
	if (ret)
		goto out;

	if (!syncobj_pended_p(sc->sobj))
		goto out;
	/*
	 * Wake up all threads waiting for this key to appear in the
	 * dictionary.
	 */
	syncobj_for_each_waiter_safe(sc->sobj, thobj, tmp) {
		wait = threadobj_get_wait(thobj);
		if (*wait->name == *name && strcmp(wait->name, name) == 0)
			syncobj_wakeup_waiter(sc->sobj, thobj);
	}
out:
	syncobj_unlock(sc->sobj, &syns);

	return ret;
}

int syncluster_delobj(struct syncluster *sc,
		      struct clusterobj *cobj)
{
	struct syncstate syns;
	int ret;

	if (syncobj_lock(sc->sobj, &syns))
		return __bt(-EINVAL);

	ret = __bt(cluster_delobj(&sc->c, cobj));

	syncobj_unlock(sc->sobj, &syns);

	return ret;
}

int syncluster_findobj(struct syncluster *sc,
		       const char *name,
		       const struct timespec *timeout,
		       struct clusterobj **cobjp)
{
	struct syncluster_wait_struct *wait = NULL;
	struct clusterobj *cobj;
	struct syncstate syns;
	int ret = 0;

	if (syncobj_lock(sc->sobj, &syns))
		return __bt(-EINVAL);

	for (;;) {
		cobj = cluster_findobj(&sc->c, name);
		if (cobj) {
			*cobjp = cobj;
			break;
		}
		if (timeout->tv_sec == 0 && timeout->tv_nsec == 0) {
			ret = -EWOULDBLOCK;
			break;
		}
		if (!threadobj_current_p()) {
			ret = -EPERM;
			break;
		}
		if (wait == NULL) {
			wait = threadobj_alloc_wait(struct syncluster_wait_struct);
			if (wait == NULL) {
				return __bt(-ENOMEM);
			}
		}
		ret = syncobj_pend(sc->sobj, timeout, &syns);
		if (ret) {
			if (ret == -EIDRM)
				goto out;
			break;
		}
	}

	syncobj_unlock(sc->sobj, &syns);
out:
	if (wait)
		threadobj_free_wait(wait);

	return ret;
}

#endif /* !CONFIG_XENO_PSHARED */

int pvcluster_init(struct pvcluster *c, const char *name)
{
	pvhash_init(&c->table);
	return 0;
}

void pvcluster_destroy(struct pvcluster *c)
{
	/* nop */
}

int pvcluster_addobj(struct pvcluster *c, const char *name,
		     struct pvclusterobj *cobj)
{
	return __bt(pvhash_enter(&c->table, name, &cobj->hobj));
}

int pvcluster_addobj_dup(struct pvcluster *c, const char *name,
			 struct pvclusterobj *cobj)
{
	return __bt(pvhash_enter_dup(&c->table, name, &cobj->hobj));
}

int pvcluster_delobj(struct pvcluster *c, struct pvclusterobj *cobj)
{
	return __bt(pvhash_remove(&c->table, &cobj->hobj));
}

struct pvclusterobj *pvcluster_findobj(struct pvcluster *c, const char *name)
{
	struct pvhashobj *hobj;

	hobj = pvhash_search(&c->table, name);
	if (hobj == NULL)
		return NULL;

	return container_of(hobj, struct pvclusterobj, hobj);
}

int pvsyncluster_init(struct pvsyncluster *sc, const char *name)
{
	int ret;

	ret = __bt(pvcluster_init(&sc->c, name));
	if (ret)
		return ret;

	/*
	 * Assuming pvcluster_destroy() is a nop, so we don't need to
	 * run any finalizer.
	 */
	syncobj_init(&sc->sobj, SYNCOBJ_FIFO, fnref_null);

	return 0;
}

void pvsyncluster_destroy(struct pvsyncluster *sc)
{
	struct syncstate syns;

	if (__bt(syncobj_lock(&sc->sobj, &syns)))
		return;

	/* No finalizer, we just destroy the synchro. */
	syncobj_destroy(&sc->sobj, &syns);
}

int pvsyncluster_addobj(struct pvsyncluster *sc, const char *name,
			struct pvclusterobj *cobj)
{
	struct syncstate syns;
	int ret;

	if (syncobj_lock(&sc->sobj, &syns))
		return __bt(-EINVAL);

	ret = __bt(pvcluster_addobj(&sc->c, name, cobj));

	syncobj_unlock(&sc->sobj, &syns);

	return ret;
}

int pvsyncluster_delobj(struct pvsyncluster *sc,
			struct pvclusterobj *cobj)
{
	struct syncstate syns;
	int ret;

	if (syncobj_lock(&sc->sobj, &syns))
		return __bt(-EINVAL);

	ret = __bt(pvcluster_delobj(&sc->c, cobj));

	syncobj_unlock(&sc->sobj, &syns);

	return ret;
}

int pvsyncluster_findobj(struct pvsyncluster *sc,
			 const char *name,
			 const struct timespec *timeout,
			 struct pvclusterobj **cobjp)
{
	struct syncluster_wait_struct *wait = NULL;
	struct pvclusterobj *cobj;
	struct syncstate syns;
	int ret = 0;

	if (syncobj_lock(&sc->sobj, &syns))
		return __bt(-EINVAL);

	for (;;) {
		cobj = pvcluster_findobj(&sc->c, name);
		if (cobj) {
			*cobjp = cobj;
			break;
		}
		if (timeout->tv_sec == 0 && timeout->tv_nsec == 0) {
			ret = -EWOULDBLOCK;
			break;
		}
		if (!threadobj_current_p()) {
			ret = -EPERM;
			break;
		}
		if (wait == NULL) {
			wait = threadobj_alloc_wait(struct syncluster_wait_struct);
			if (wait == NULL) {
				return __bt(-ENOMEM);
			}
		}
		ret = syncobj_pend(&sc->sobj, timeout, &syns);
		if (ret) {
			if (ret == -EIDRM)
				goto out;
			break;
		}
	}

	syncobj_unlock(&sc->sobj, &syns);
out:
	if (wait)
		threadobj_free_wait(wait);

	return ret;
}

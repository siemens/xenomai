/**
 * @file
 * This file is part of the Xenomai project.
 *
 * @note Copyright (C) 2004 Philippe Gerum <rpm@xenomai.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * \ingroup registry
 */

/*!
 * \ingroup nucleus
 * \defgroup registry Registry services.
 *
 * The registry provides a mean to index real-time object descriptors
 * created by Xenomai skins on unique alphanumeric keys. When labeled
 * this way, a real-time object is globally exported; it can be
 * searched for, and its descriptor returned to the caller for further
 * use; the latter operation is called a "binding". When no object has
 * been registered under the given name yet, the registry can be asked
 * to set up a rendez-vous, blocking the caller until the object is
 * eventually registered.
 *
 *@{*/

#include <nucleus/pod.h>
#include <nucleus/heap.h>
#include <nucleus/registry.h>
#include <nucleus/thread.h>
#include <nucleus/assert.h>

#ifndef CONFIG_XENO_OPT_DEBUG_REGISTRY
#define CONFIG_XENO_OPT_DEBUG_REGISTRY  0
#endif

struct xnobject *registry_obj_slots;
EXPORT_SYMBOL_GPL(registry_obj_slots);

static struct xnqueue registry_obj_freeq;	/* Free objects. */

static struct xnqueue registry_obj_busyq;	/* Active and exported objects. */

static u_long registry_obj_stamp;

static struct xnobject **registry_hash_table;

static int registry_hash_entries;

static struct xnsynch registry_hash_synch;

#ifdef CONFIG_XENO_OPT_VFILE

#include <linux/workqueue.h>

static unsigned registry_exported_objects;

static DECLARE_WORK_FUNC(registry_proc_callback);

static void registry_proc_schedule(void *cookie);

static xnqueue_t registry_obj_procq;	/* Objects waiting for /proc handling. */

static DECLARE_WORK_NODATA(registry_proc_work, &registry_proc_callback);

static int registry_proc_apc;

static struct xnvfile_directory registry_vfroot;

static int usage_vfile_show(struct xnvfile_regular_iterator *it, void *data)
{
	if (!xnpod_active_p())
		return -ESRCH;

	xnvfile_printf(it, "slots=%u:used=%u:exported=%u\n",
		       CONFIG_XENO_OPT_REGISTRY_NRSLOTS,
		       CONFIG_XENO_OPT_REGISTRY_NRSLOTS -
		       countq(&registry_obj_freeq),
		       registry_exported_objects);
	return 0;
}

static struct xnvfile_regular_ops usage_vfile_ops = {
	.show = usage_vfile_show,
};

static struct xnvfile_regular usage_vfile = {
	.ops = &usage_vfile_ops,
};

#endif /* CONFIG_XENO_OPT_VFILE */

int xnregistry_init(void)
{
	static const int primes[] = {
		101, 211, 307, 401, 503, 601,
		701, 809, 907, 1009, 1103
	};

#define obj_hash_max(n)			 \
((n) < sizeof(primes) / sizeof(int) ? \
 (n) : sizeof(primes) / sizeof(int) - 1)

	int n, ret;

	registry_obj_slots =
		xnarch_alloc_host_mem(CONFIG_XENO_OPT_REGISTRY_NRSLOTS * sizeof(struct xnobject));
	if (registry_obj_slots == NULL)
		return -ENOMEM;

#ifdef CONFIG_XENO_OPT_VFILE
	ret = xnvfile_init_dir("registry", &registry_vfroot, &nkvfroot);
	if (ret)
		return ret;

	ret = xnvfile_init_regular("usage", &usage_vfile, &registry_vfroot);
	if (ret) {
		xnvfile_destroy_dir(&registry_vfroot);
		return ret;
	}

	registry_proc_apc =
	    rthal_apc_alloc("registry_export", &registry_proc_schedule, NULL);

	if (registry_proc_apc < 0) {
		xnvfile_destroy_regular(&usage_vfile);
		xnvfile_destroy_dir(&registry_vfroot);
		return registry_proc_apc;
	}

	initq(&registry_obj_procq);
#endif /* CONFIG_XENO_OPT_VFILE */

	initq(&registry_obj_freeq);
	initq(&registry_obj_busyq);
	registry_obj_stamp = 0;

	for (n = 0; n < CONFIG_XENO_OPT_REGISTRY_NRSLOTS; n++) {
		inith(&registry_obj_slots[n].link);
		registry_obj_slots[n].objaddr = NULL;
		appendq(&registry_obj_freeq, &registry_obj_slots[n].link);
	}

	getq(&registry_obj_freeq);	/* Slot #0 is reserved/invalid. */

	registry_hash_entries =
	    primes[obj_hash_max(CONFIG_XENO_OPT_REGISTRY_NRSLOTS / 100)];
	registry_hash_table = xnarch_alloc_host_mem(sizeof(struct xnobject *) *
						    registry_hash_entries);

	if (registry_hash_table == NULL) {
#ifdef CONFIG_XENO_OPT_VFILE
		xnvfile_destroy_regular(&usage_vfile);
		xnvfile_destroy_dir(&registry_vfroot);
		rthal_apc_free(registry_proc_apc);
#endif /* CONFIG_XENO_OPT_VFILE */
		return -ENOMEM;
	}

	for (n = 0; n < registry_hash_entries; n++)
		registry_hash_table[n] = NULL;

	xnsynch_init(&registry_hash_synch, XNSYNCH_FIFO, NULL);

	return 0;
}

void xnregistry_cleanup(void)
{
#ifdef CONFIG_XENO_OPT_VFILE
	struct xnobject *ecurr, *enext;
	struct xnpnode *pnode;
	int n;

	flush_scheduled_work();

	for (n = 0; n < registry_hash_entries; n++)
		for (ecurr = registry_hash_table[n]; ecurr; ecurr = enext) {
			enext = ecurr->hnext;
			pnode = ecurr->pnode;
			if (pnode == NULL)
				continue;

			pnode->ops->unexport(ecurr, pnode);

			if (--pnode->entries > 0)
				continue;

			xnvfile_destroy_dir(&pnode->vdir);

			if (--pnode->root->entries == 0)
				xnvfile_destroy_dir(&pnode->root->vdir);
	}
#endif /* CONFIG_XENO_OPT_VFILE */

	xnarch_free_host_mem(registry_hash_table,
		       sizeof(struct xnobject *) * registry_hash_entries);

	xnsynch_destroy(&registry_hash_synch);

#ifdef CONFIG_XENO_OPT_VFILE
	rthal_apc_free(registry_proc_apc);
	flush_scheduled_work();
	xnvfile_destroy_regular(&usage_vfile);
	xnvfile_destroy_dir(&registry_vfroot);
#endif /* CONFIG_XENO_OPT_VFILE */

	xnarch_free_host_mem(registry_obj_slots,
			     CONFIG_XENO_OPT_REGISTRY_NRSLOTS * sizeof(struct xnobject));
}

#ifdef CONFIG_XENO_OPT_VFILE

static DEFINE_BINARY_SEMAPHORE(export_mutex);

/*
 * The following stuff implements the mechanism for delegating
 * export/unexport requests to/from the /proc interface from the
 * Xenomai domain to the Linux kernel (i.e. the "lower stage"). This
 * ends up being a bit complex due to the fact that such requests
 * might lag enough before being processed by the Linux kernel so that
 * subsequent requests might just contradict former ones before they
 * even had a chance to be applied (e.g. export -> unexport in the
 * Xenomai domain for short-lived objects). This situation and the
 * like are hopefully properly handled due to a careful
 * synchronization of operations across domains.
 */
static DECLARE_WORK_FUNC(registry_proc_callback)
{
	struct xnvfile_directory *rdir, *dir;
	const char *rname, *type;
	struct xnholder *holder;
	struct xnobject *object;
	struct xnpnode *pnode;
	int ret;
	spl_t s;

	down(&export_mutex);

	xnlock_get_irqsave(&nklock, s);

	while ((holder = getq(&registry_obj_procq)) != NULL) {
		object = link2xnobj(holder);
		pnode = object->pnode;
		type = pnode->dirname;
		dir = &pnode->vdir;
		rdir = &pnode->root->vdir;
		rname = pnode->root->dirname;

		if (object->vfilp != XNOBJECT_PNODE_RESERVED1)
			goto unexport;

		registry_exported_objects++;
		object->vfilp = XNOBJECT_PNODE_RESERVED2;
		appendq(&registry_obj_busyq, holder);

		xnlock_put_irqrestore(&nklock, s);

		if (pnode->entries++ == 0) {
			if (pnode->root->entries++ == 0) {
				/* Create the root directory on the fly. */
				ret = xnvfile_init_dir(rname, rdir, &registry_vfroot);
				if (ret) {
					xnlock_get_irqsave(&nklock, s);
					object->pnode = NULL;
					pnode->root->entries = 0;
					pnode->entries = 0;
					continue;
				}
			}
			/* Create the class directory on the fly. */
			ret = xnvfile_init_dir(type, dir, rdir);
			if (ret) {
				if (pnode->root->entries == 1) {
					pnode->root->entries = 0;
					xnvfile_destroy_dir(rdir);
				}
				xnlock_get_irqsave(&nklock, s);
				object->pnode = NULL;
				pnode->entries = 0;
				continue;
			}
		}

		ret = pnode->ops->export(object, pnode);
		if (ret && --pnode->entries == 0) {
			xnvfile_destroy_dir(dir);
			if (--pnode->root->entries == 0)
				xnvfile_destroy_dir(rdir);
			xnlock_get_irqsave(&nklock, s);
			object->pnode = NULL;
		} else
			xnlock_get_irqsave(&nklock, s);

		continue;

	unexport:
		registry_exported_objects--;
		object->vfilp = NULL;
		object->pnode = NULL;

		if (object->objaddr)
			appendq(&registry_obj_busyq, holder);
		else
			/*
			 * Trap the case where we are unexporting an
			 * already unregistered object.
			 */
			appendq(&registry_obj_freeq, holder);

		xnlock_put_irqrestore(&nklock, s);

		pnode->ops->unexport(object, pnode);

		if (--pnode->entries == 0) {
			xnvfile_destroy_dir(dir);
			if (--pnode->root->entries == 0)
				xnvfile_destroy_dir(rdir);
		}

		xnlock_get_irqsave(&nklock, s);
	}

	xnlock_put_irqrestore(&nklock, s);

	up(&export_mutex);
}

static void registry_proc_schedule(void *cookie)
{
	/*
	 * schedule_work() will check for us if the work has already
	 * been scheduled, so just be lazy and submit blindly.
	 */
	schedule_work(&registry_proc_work);
}

static int registry_export_vfsnap(struct xnobject *object,
				  struct xnpnode *pnode)
{
	struct xnpnode_snapshot *p;
	int ret;

	/*
	 * Make sure to initialize _all_ mandatory vfile fields; most
	 * of the time we are using sane NULL defaults based on static
	 * storage for the vfile struct, but here we are building up a
	 * vfile object explicitly.
	 */
	p = container_of(pnode, struct xnpnode_snapshot, node);
	object->vfile_u.vfsnap.file.datasz = p->vfile.datasz;
	object->vfile_u.vfsnap.file.privsz = p->vfile.privsz;
	/*
	 * Make the vfile refer to the provided tag struct if any,
	 * otherwise use our default tag space. In the latter case,
	 * each object family has its own private revision tag.
	 */
	object->vfile_u.vfsnap.file.tag = p->vfile.tag ?:
		&object->vfile_u.vfsnap.tag;
	object->vfile_u.vfsnap.file.ops = p->vfile.ops;
	object->vfile_u.vfsnap.file.entry.lockops = p->vfile.lockops;

	ret = xnvfile_init_snapshot(object->key, &object->vfile_u.vfsnap.file,
				    &pnode->vdir);
	if (ret)
		return ret;

	object->vfilp = &object->vfile_u.vfsnap.file.entry;
	object->vfilp->private = object->objaddr;

	return 0;
}

static void registry_unexport_vfsnap(struct xnobject *object,
				    struct xnpnode *pnode)
{
	xnvfile_destroy_snapshot(&object->vfile_u.vfsnap.file);
}

static void registry_touch_vfsnap(struct xnobject *object)
{
	xnvfile_touch(&object->vfile_u.vfsnap.file);
}

struct xnpnode_ops xnregistry_vfsnap_ops = {
	.export = registry_export_vfsnap,
	.unexport = registry_unexport_vfsnap,
	.touch = registry_touch_vfsnap,
};
EXPORT_SYMBOL_GPL(xnregistry_vfsnap_ops);

static int registry_export_vfreg(struct xnobject *object,
				 struct xnpnode *pnode)
{
	struct xnpnode_regular *p;
	int ret;

	/* See registry_export_vfsnap() for hints. */
	p = container_of(pnode, struct xnpnode_regular, node);
	object->vfile_u.vfreg.privsz = p->vfile.privsz;
	object->vfile_u.vfreg.ops = p->vfile.ops;
	object->vfile_u.vfreg.entry.lockops = p->vfile.lockops;

	ret = xnvfile_init_regular(object->key, &object->vfile_u.vfreg,
				   &pnode->vdir);
	if (ret)
		return ret;

	object->vfilp = &object->vfile_u.vfreg.entry;
	object->vfilp->private = object->objaddr;

	return 0;
}

static void registry_unexport_vfreg(struct xnobject *object,
				    struct xnpnode *pnode)
{
	xnvfile_destroy_regular(&object->vfile_u.vfreg);
}

struct xnpnode_ops xnregistry_vfreg_ops = {
	.export = registry_export_vfreg,
	.unexport = registry_unexport_vfreg,
};
EXPORT_SYMBOL_GPL(xnregistry_vfreg_ops);

static int registry_export_vlink(struct xnobject *object,
				 struct xnpnode *pnode)
{
	struct xnpnode_link *link_desc;
	char *link_target;
	int ret;

	link_desc = container_of(pnode, struct xnpnode_link, node);
	link_target = link_desc->target(object->objaddr);
	if (link_target == NULL)
		return -ENOMEM;

	ret = xnvfile_init_link(object->key, link_target,
				&object->vfile_u.link, &pnode->vdir);
	kfree(link_target);
	if (ret)
		return ret;

	object->vfilp = &object->vfile_u.link.entry;
	object->vfilp->private = object->objaddr;

	return 0;
}

static void registry_unexport_vlink(struct xnobject *object,
				    struct xnpnode *pnode)
{
	xnvfile_destroy_link(&object->vfile_u.link);
}

struct xnpnode_ops xnregistry_vlink_ops = {
	.export = registry_export_vlink,
	.unexport = registry_unexport_vlink,
};
EXPORT_SYMBOL_GPL(xnregistry_vlink_ops);

static inline void registry_export_pnode(struct xnobject *object,
					 struct xnpnode *pnode)
{
	object->vfilp = XNOBJECT_PNODE_RESERVED1;
	object->pnode = pnode;
	removeq(&registry_obj_busyq, &object->link);
	appendq(&registry_obj_procq, &object->link);
	__rthal_apc_schedule(registry_proc_apc);
}

static inline void registry_unexport_pnode(struct xnobject *object)
{
	if (object->vfilp != XNOBJECT_PNODE_RESERVED1) {
		/*
		 * We might have preempted a v-file read op, so bump
		 * the object's revtag to make sure the data
		 * collection is aborted next, if we end up deleting
		 * the object being read.
		 */
		if (object->pnode->ops->touch)
			object->pnode->ops->touch(object);
		removeq(&registry_obj_busyq, &object->link);
		appendq(&registry_obj_procq, &object->link);
		__rthal_apc_schedule(registry_proc_apc);
	} else {
		/*
		 * Unexporting before the lower stage has had a chance
		 * to export. Move back the object to the busyq just
		 * like if no export had been requested.
		 */
		removeq(&registry_obj_procq, &object->link);
		appendq(&registry_obj_busyq, &object->link);
		object->pnode = NULL;
		object->vfilp = NULL;
	}
}

#endif /* CONFIG_XENO_OPT_VFILE */

static unsigned registry_hash_crunch(const char *key)
{
	unsigned int h = 0, g;

#define HQON    24		/* Higher byte position */
#define HBYTE   0xf0000000	/* Higher nibble on */

	while (*key) {
		h = (h << 4) + *key++;
		if ((g = (h & HBYTE)) != 0)
			h = (h ^ (g >> HQON)) ^ g;
	}

	return h % registry_hash_entries;
}

static inline int registry_hash_enter(const char *key, struct xnobject *object)
{
	struct xnobject *ecurr;
	unsigned s;

	object->key = key;
	s = registry_hash_crunch(key);

	for (ecurr = registry_hash_table[s]; ecurr != NULL; ecurr = ecurr->hnext) {
		if (ecurr == object || !strcmp(key, ecurr->key))
			return -EEXIST;
	}

	object->hnext = registry_hash_table[s];
	registry_hash_table[s] = object;

	return 0;
}

static inline int registry_hash_remove(struct xnobject *object)
{
	unsigned s = registry_hash_crunch(object->key);
	struct xnobject *ecurr, *eprev;

	for (ecurr = registry_hash_table[s], eprev = NULL;
	     ecurr != NULL; eprev = ecurr, ecurr = ecurr->hnext) {
		if (ecurr == object) {
			if (eprev)
				eprev->hnext = ecurr->hnext;
			else
				registry_hash_table[s] = ecurr->hnext;

			return 0;
		}
	}

	return -ESRCH;
}

static struct xnobject *registry_hash_find(const char *key)
{
	struct xnobject *ecurr;

	for (ecurr = registry_hash_table[registry_hash_crunch(key)];
	     ecurr != NULL; ecurr = ecurr->hnext) {
		if (!strcmp(key, ecurr->key))
			return ecurr;
	}

	return NULL;
}

static inline unsigned registry_wakeup_sleepers(const char *key)
{
	xnpholder_t *holder, *nholder;
	unsigned cnt = 0;

	nholder = getheadpq(xnsynch_wait_queue(&registry_hash_synch));

	while ((holder = nholder) != NULL) {
		xnthread_t *sleeper = link2thread(holder, plink);

		if (*key == *sleeper->registry.waitkey &&
		    !strcmp(key, sleeper->registry.waitkey)) {
			sleeper->registry.waitkey = NULL;
			nholder =
			    xnsynch_wakeup_this_sleeper(&registry_hash_synch,
							holder);
			++cnt;
		} else
			nholder =
			    nextpq(xnsynch_wait_queue(&registry_hash_synch),
				   holder);
	}

	return cnt;
}

/**
 * @fn int xnregistry_enter(const char *key,void *objaddr,xnhandle_t *phandle,struct xnpnode *pnode)
 * @brief Register a real-time object.
 *
 * This service allocates a new registry slot for an associated
 * object, and indexes it by an alphanumeric key for later retrieval.
 *
 * @param key A valid NULL-terminated string by which the object will
 * be indexed and later retrieved in the registry. Since it is assumed
 * that such key is stored into the registered object, it will *not*
 * be copied but only kept by reference in the registry. Pass an empty
 * string if the object shall only occupy a registry slot
 * for handle-based lookups.
 *
 * @param objaddr An opaque pointer to the object to index by @a
 * key.
 *
 * @param phandle A pointer to a generic handle defined by the
 * registry which will uniquely identify the indexed object, until the
 * latter is unregistered using the xnregistry_remove() service.
 *
 * @param pnode A pointer to an optional /proc node class
 * descriptor. This structure provides the information needed to
 * export all objects from the given class through the /proc
 * filesystem, under the /proc/xenomai/registry entry. Passing NULL
 * indicates that no /proc support is available for the newly
 * registered object.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a objaddr are NULL, or if @a key constains
 * an invalid '/' character.
 *
 * - -ENOMEM is returned if the system fails to get enough dynamic
 * memory from the global real-time heap in order to register the
 * object.
 *
 * - -EEXIST is returned if the @a key is already in use.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based thread
 *
 * Rescheduling: possible.
 */

int xnregistry_enter(const char *key, void *objaddr,
		     xnhandle_t *phandle, struct xnpnode *pnode)
{
	struct xnholder *holder;
	struct xnobject *object;
	spl_t s;
	int ret;

	if (key == NULL || objaddr == NULL || strchr(key, '/'))
		return -EINVAL;

	xnlock_get_irqsave(&nklock, s);

	holder = getq(&registry_obj_freeq);
	if (holder == NULL) {
		ret = -ENOMEM;
		goto unlock_and_exit;
	}

	object = link2xnobj(holder);
	xnsynch_init(&object->safesynch, XNSYNCH_FIFO, NULL);
	object->objaddr = objaddr;
	object->cstamp = ++registry_obj_stamp;
	object->safelock = 0;
#ifdef CONFIG_XENO_OPT_VFILE
	object->pnode = NULL;
#endif
	if (*key == '\0') {
		object->key = NULL;
		*phandle = object - registry_obj_slots;
		ret = 0;
		goto unlock_and_exit;
	}

	ret = registry_hash_enter(key, object);
	if (ret) {
		appendq(&registry_obj_freeq, holder);
		goto unlock_and_exit;
	}

	appendq(&registry_obj_busyq, holder);

	/*
	 * <!> Make sure the handle is written back before the
	 * rescheduling takes place.
	 */
	*phandle = object - registry_obj_slots;

#ifdef CONFIG_XENO_OPT_VFILE
	if (pnode)
		registry_export_pnode(object, pnode);
#endif /* CONFIG_XENO_OPT_VFILE */

	if (registry_wakeup_sleepers(key))
		xnpod_schedule();

unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

#if XENO_DEBUG(REGISTRY)
	if (ret)
		xnlogerr("FAILED to register object %s (%s), status %d\n",
			 key,
			 pnode ? pnode->dirname : "unknown type",
			 ret);
	else if (pnode)
		xnloginfo("registered exported object %s (%s)\n",
			  key, pnode->dirname);
#endif

	return ret;
}
EXPORT_SYMBOL_GPL(xnregistry_enter);

/**
 * @fn int xnregistry_bind(const char *key,xnticks_t timeout,int timeout_mode,xnhandle_t *phandle)
 * @brief Bind to a real-time object.
 *
 * This service retrieves the registry handle of a given object
 * identified by its key. Unless otherwise specified, this service
 * will block the caller if the object is not registered yet, waiting
 * for such registration to occur.
 *
 * @param key A valid NULL-terminated string which identifies the
 * object to bind to.
 *
 * @param timeout The timeout which may be used to limit the time the
 * thread wait for the object to be registered. This value is a wait
 * time given in ticks (see note). It can either be relative, absolute
 * monotonic (XN_ABSOLUTE), or absolute adjustable (XN_REALTIME)
 * depending on @a timeout_mode. Passing XN_INFINITE @b and setting @a
 * timeout_mode to XN_RELATIVE specifies an unbounded wait. Passing
 * XN_NONBLOCK causes the service to return immediately without
 * waiting if the object is not registered on entry. All other values
 * are used as a wait limit.
 *
 * @param timeout_mode The mode of the @a timeout parameter. It can
 * either be set to XN_RELATIVE, XN_ABSOLUTE, or XN_REALTIME (see also
 * xntimer_start()).
 *
 * @param phandle A pointer to a memory location which will be written
 * upon success with the generic handle defined by the registry for
 * the retrieved object. Contents of this memory is undefined upon
 * failure.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a key is NULL.
 *
 * - -EINTR is returned if xnpod_unblock_thread() has been called for
 * the waiting thread before the retrieval has completed.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to XN_NONBLOCK
 * and the searched object is not registered on entry. As a special
 * exception, this error is also returned if this service should
 * block, but was called from a context which cannot sleep
 * (e.g. interrupt, non-realtime or scheduler locked).
 *
 * - -ETIMEDOUT is returned if the object cannot be retrieved within
 * the specified amount of time.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 *   only if @a timeout is equal to XN_NONBLOCK.
 *
 * - Kernel-based thread.
 *
 * Rescheduling: always unless the request is immediately satisfied or
 * @a timeout specifies a non-blocking operation.
 *
 * @note The @a timeout value will be interpreted as jiffies if @a
 * thread is bound to a periodic time base (see xnpod_init_thread), or
 * nanoseconds otherwise.
 */

int xnregistry_bind(const char *key, xnticks_t timeout, int timeout_mode,
		    xnhandle_t *phandle)
{
	struct xnobject *object;
	xnthread_t *thread;
	xntbase_t *tbase;
	int err = 0;
	spl_t s;

	if (!key)
		return -EINVAL;

	thread = xnpod_current_thread();
	tbase = xnthread_time_base(thread);

	xnlock_get_irqsave(&nklock, s);

	if (timeout_mode == XN_RELATIVE &&
	    timeout != XN_INFINITE && timeout != XN_NONBLOCK) {
		timeout_mode = XN_REALTIME;
		timeout += xntbase_get_time(tbase);
	}

	for (;;) {
		object = registry_hash_find(key);

		if (object) {
			*phandle = object - registry_obj_slots;
			goto unlock_and_exit;
		}

		if ((timeout_mode == XN_RELATIVE && timeout == XN_NONBLOCK) ||
		    xnpod_unblockable_p()) {
			err = -EWOULDBLOCK;
			goto unlock_and_exit;
		}

		thread->registry.waitkey = key;
		xnsynch_sleep_on(&registry_hash_synch, timeout, timeout_mode);

		if (xnthread_test_info(thread, XNTIMEO)) {
			err = -ETIMEDOUT;
			goto unlock_and_exit;
		}

		if (xnthread_test_info(thread, XNBREAK)) {
			err = -EINTR;
			goto unlock_and_exit;
		}
	}

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}
EXPORT_SYMBOL_GPL(xnregistry_bind);

/**
 * @fn int xnregistry_remove(xnhandle_t handle)
 * @brief Forcibly unregister a real-time object.
 *
 * This service forcibly removes an object from the registry. The
 * removal is performed regardless of the current object's locking
 * status.
 *
 * @param handle The generic handle of the object to remove.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -ESRCH is returned if @a handle does not reference a registered
 * object.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based thread
 *
 * Rescheduling: never.
 */

int xnregistry_remove(xnhandle_t handle)
{
	struct xnobject *object;
	int err = 0;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	object = xnregistry_validate(handle);
	if (object == NULL) {
		err = -ESRCH;
		goto unlock_and_exit;
	}

#if XENO_DEBUG(REGISTRY)
	/* We must keep the lock and report early, when the object
	 * slot is still valid. Note: we only report about exported
	 * objects. */
	if (object->pnode)
		xnloginfo("unregistered exported object %s (%s)\n",
			  object->key,
			  object->pnode->dirname);
#endif

	object->objaddr = NULL;
	object->cstamp = 0;

	if (object->key) {
		registry_hash_remove(object);

#ifdef CONFIG_XENO_OPT_VFILE
		if (object->pnode) {
			registry_unexport_pnode(object);

			/* Leave the update of the object queues to the work
			   callback if it has been kicked. */

			if (object->pnode)
				goto unlock_and_exit;
		}
#endif /* CONFIG_XENO_OPT_VFILE */

		removeq(&registry_obj_busyq, &object->link);
	}

	appendq(&registry_obj_freeq, &object->link);

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}
EXPORT_SYMBOL_GPL(xnregistry_remove);

/**
 * @fn int xnregistry_remove_safe(xnhandle_t handle,xnticks_t timeout)
 * @brief Unregister an idle real-time object.
 *
 * This service removes an object from the registry. The caller might
 * sleep as a result of waiting for the target object to be unlocked
 * prior to the removal (see xnregistry_put()).
 *
 * @param handle The generic handle of the object to remove.
 *
 * @param timeout If the object is locked on entry, @a param gives the
 * number of clock ticks to wait for the unlocking to occur (see
 * note). Passing XN_INFINITE causes the caller to block
 * indefinitely until the object is unlocked. Passing XN_NONBLOCK
 * causes the service to return immediately without waiting if the
 * object is locked on entry.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -ESRCH is returned if @a handle does not reference a registered
 * object.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to XN_NONBLOCK
 * and the object is locked on entry.
 *
 * - -EBUSY is returned if @a handle refers to a locked object and the
 * caller could not sleep until it is unlocked.
 *
 * - -ETIMEDOUT is returned if the object cannot be removed within the
 * specified amount of time.
 *
 * - -EINTR is returned if xnpod_unblock_thread() has been called for
 * the calling thread waiting for the object to be unlocked.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 *   only if @a timeout is equal to XN_NONBLOCK.
 *
 * - Kernel-based thread.
 *
 * Rescheduling: possible if the object to remove is currently locked
 * and the calling context can sleep.
 *
 * @note The @a timeout value will be interpreted as jiffies if the
 * current thread is bound to a periodic time base (see
 * xnpod_init_thread), or nanoseconds otherwise.
 */

int xnregistry_remove_safe(xnhandle_t handle, xnticks_t timeout)
{
	struct xnobject *object;
	u_long cstamp;
	int err = 0;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	object = xnregistry_validate(handle);
	if (object == NULL) {
		err = -ESRCH;
		goto unlock_and_exit;
	}

	if (object->safelock == 0)
		goto remove;

	if (timeout == XN_NONBLOCK) {
		err = -EWOULDBLOCK;
		goto unlock_and_exit;
	}

	if (xnpod_unblockable_p()) {
		err = -EBUSY;
		goto unlock_and_exit;
	}

	/*
	 * The object creation stamp is here to deal with situations like this
	 * one:
	 *
	 * Thread(A) locks Object(T) using xnregistry_get()
	 * Thread(B) attempts to remove Object(T) using xnregistry_remove()
	 * Thread(C) attempts the same removal, waiting like Thread(B) for
	 * the object's safe count to fall down to zero.
	 * Thread(A) unlocks Object(T), unblocking Thread(B) and (C).
	 * Thread(B) wakes up and successfully removes Object(T)
	 * Thread(D) preempts Thread(C) and recycles Object(T) for another object
	 * Thread(C) wakes up and attempts to finalize the removal of the
	 * _former_ Object(T), which leads to the spurious removal of the
	 * _new_ Object(T).
	 */

	cstamp = object->cstamp;

	do {
		xnsynch_sleep_on(&object->safesynch, timeout, XN_RELATIVE);

		if (xnthread_test_info(xnpod_current_thread(), XNBREAK)) {
			err = -EINTR;
			goto unlock_and_exit;
		}

		if (xnthread_test_info(xnpod_current_thread(), XNTIMEO)) {
			err = -ETIMEDOUT;
			goto unlock_and_exit;
		}
	}
	while (object->safelock > 0);

	if (object->cstamp != cstamp) {
		/* The caller should silently abort the removal process. */
		err = -ESRCH;
		goto unlock_and_exit;
	}

      remove:

	err = xnregistry_remove(handle);

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}
EXPORT_SYMBOL_GPL(xnregistry_remove_safe);

/**
 * @fn void *xnregistry_get(xnhandle_t handle)
 * @brief Find and lock a real-time object into the registry.
 *
 * This service retrieves an object from its handle into the registry
 * and prevents it removal atomically. A locking count is tracked, so
 * that xnregistry_get() and xnregistry_put() must be used in pair.
 *
 * @param handle The generic handle of the object to find and lock. If
 * XNOBJECT_SELF is passed, the object is the calling Xenomai
 * thread.
 *
 * @return The memory address of the object's descriptor is returned
 * on success. Otherwise, NULL is returned if @a handle does not
 * reference a registered object, or if @a handle is equal to
 * XNOBJECT_SELF but the current context is not a real-time thread.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * only if @a handle is different from XNOBJECT_SELF.
 *
 * - Kernel-based thread.
 *
 * Rescheduling: never.
 */

void *xnregistry_get(xnhandle_t handle)
{
	struct xnobject *object;
	void *objaddr;
	spl_t s;

	if (handle == XNOBJECT_SELF) {
		if (!xnpod_primary_p())
			return NULL;
		handle = xnpod_current_thread()->registry.handle;
	}

	xnlock_get_irqsave(&nklock, s);

	object = xnregistry_validate(handle);
	if (likely(object != NULL)) {
		++object->safelock;
		objaddr = object->objaddr;
	} else
		objaddr = NULL;

	xnlock_put_irqrestore(&nklock, s);

	return objaddr;
}
EXPORT_SYMBOL_GPL(xnregistry_get);

/**
 * @fn u_long xnregistry_put(xnhandle_t handle)
 * @brief Unlock a real-time object from the registry.
 *
 * This service decrements the lock count of a registered object
 * previously locked by a call to xnregistry_get(). The object is
 * actually unlocked from the registry when the locking count falls
 * down to zero, thus waking up any thread currently blocked on
 * xnregistry_remove() for unregistering it.
 *
 * @param handle The generic handle of the object to unlock. If
 * XNOBJECT_SELF is passed, the object is the calling Xenomai thread.
 *
 * @return The decremented lock count is returned upon success. Zero
 * is also returned if @a handle does not reference a registered
 * object, or if @a handle is equal to XNOBJECT_SELF but the current
 * context is not a real-time thread.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * only if @a handle is different from XNOBJECT_SELF.
 *
 * - Kernel-based thread
 *
 * Rescheduling: possible if the lock count falls down to zero and
 * some thread is currently waiting for the object to be unlocked.
 */

u_long xnregistry_put(xnhandle_t handle)
{
	struct xnobject *object;
	u_long newlock;
	spl_t s;

	if (handle == XNOBJECT_SELF) {
		if (!xnpod_primary_p())
			return 0;
		handle = xnpod_current_thread()->registry.handle;
	}

	xnlock_get_irqsave(&nklock, s);

	object = xnregistry_validate(handle);
	if (object == NULL) {
		newlock = 0;
		goto unlock_and_exit;
	}

	if ((newlock = object->safelock) > 0 &&
	    (newlock = --object->safelock) == 0 &&
	    xnsynch_nsleepers(&object->safesynch) > 0) {
		xnsynch_flush(&object->safesynch, 0);
		xnpod_schedule();
	}

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return newlock;
}
EXPORT_SYMBOL_GPL(xnregistry_put);

/**
 * @fn u_long xnregistry_fetch(xnhandle_t handle)
 * @brief Find a real-time object into the registry.
 *
 * This service retrieves an object from its handle into the registry
 * and returns the memory address of its descriptor.
 *
 * @param handle The generic handle of the object to fetch. If
 * XNOBJECT_SELF is passed, the object is the calling Xenomai thread.
 *
 * @return The memory address of the object's descriptor is returned
 * on success. Otherwise, NULL is returned if @a handle does not
 * reference a registered object, or if @a handle is equal to
 * XNOBJECT_SELF but the current context is not a real-time thread.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * only if @a handle is different from XNOBJECT_SELF.
 *
 * - Kernel-based thread
 *
 * Rescheduling: never.
 */

void *xnregistry_fetch(xnhandle_t handle)
{
 	if (handle == XNOBJECT_SELF)
 		return xnpod_primary_p()? xnpod_current_thread() : NULL;

	return xnregistry_lookup(handle);
}
EXPORT_SYMBOL_GPL(xnregistry_fetch);

/*@}*/

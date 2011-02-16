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

#ifndef _XENO_NUCLEUS_REGISTRY_H
#define _XENO_NUCLEUS_REGISTRY_H

#include <nucleus/types.h>

#define XNOBJECT_SELF  XN_NO_HANDLE

#if defined(__KERNEL__) || defined(__XENO_SIM__)

#include <nucleus/synch.h>

struct xnpnode;

typedef struct xnobject {

    xnholder_t link;

#define link2xnobj(ln)		container_of(ln, xnobject_t, link)

    void *objaddr;

    const char *key;	/* !< Hash key. */

    xnsynch_t safesynch; /* !< Safe synchronization object. */

    u_long safelock;	 /* !< Safe lock count. */

    u_long cstamp;	/* !< Creation stamp. */

    struct xnobject *hnext;	/* !< Next in h-table */

#ifdef CONFIG_PROC_FS

    struct xnpnode *pnode; /* !< /proc information class. */

    struct proc_dir_entry *proc; /* !< /proc entry. */

#endif /* CONFIG_PROC_FS */

} xnobject_t;

#ifdef __cplusplus
extern "C" {
#endif

int xnregistry_init(void);

void xnregistry_cleanup(void);

#ifdef CONFIG_PROC_FS

#include <linux/proc_fs.h>

#define XNOBJECT_PROC_RESERVED1 ((struct proc_dir_entry *)1)
#define XNOBJECT_PROC_RESERVED2 ((struct proc_dir_entry *)2)

typedef ssize_t link_proc_t(char *buf,
			    int count,
			    void *data);
typedef struct xnptree {

    struct proc_dir_entry *dir;
    const char *name;
    int entries;

} xnptree_t;

typedef struct xnpnode {

    struct proc_dir_entry *dir;
    const char *type;
    int entries;
    read_proc_t *read_proc;
    write_proc_t *write_proc;
    link_proc_t *link_proc;
    xnptree_t *root;

} xnpnode_t;

#else /* !CONFIG_PROC_FS */

typedef struct xnpnode { /* Placeholder. */

    const char *type;

} xnpnode_t;

#endif /* !CONFIG_PROC_FS */

extern struct xnobject *registry_obj_slots;

/* Public interface. */

int xnregistry_enter(const char *key,
		     void *objaddr,
		     xnhandle_t *phandle,
		     xnpnode_t *pnode);

int xnregistry_bind(const char *key,
		    xnticks_t timeout,
		    int timeout_mode,
		    xnhandle_t *phandle);

int xnregistry_remove(xnhandle_t handle);

int xnregistry_remove_safe(xnhandle_t handle,
			   xnticks_t timeout);

void *xnregistry_get(xnhandle_t handle);

void *xnregistry_fetch(xnhandle_t handle);

u_long xnregistry_put(xnhandle_t handle);

static inline struct xnobject *xnregistry_validate(xnhandle_t handle)
{
	struct xnobject *object;
	/*
	 * Careful: a removed object which is still in flight to be
	 * unexported carries a NULL objaddr, so we have to check this
	 * as well.
	 */
	if (likely(handle && handle < CONFIG_XENO_OPT_REGISTRY_NRSLOTS)) {
		object = &registry_obj_slots[handle];
		return object->objaddr ? object : NULL;
	}

	return NULL;
}

static inline void *xnregistry_lookup(xnhandle_t handle)
{
	struct xnobject *object = xnregistry_validate(handle);
	return object ? object->objaddr : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* __KERNEL__ || __XENO_SIM__ */

#endif /* !_XENO_NUCLEUS_REGISTRY_H */

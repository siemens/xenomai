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
 */

#ifndef _XENO_REGISTRY_H
#define _XENO_REGISTRY_H

#include <native/types.h>

#define RT_REGISTRY_SELF  RT_HANDLE_INVALID

#if defined(__KERNEL__) && defined(CONFIG_PROC_FS) && defined(CONFIG_XENO_OPT_NATIVE_REGISTRY)
#define CONFIG_XENO_NATIVE_EXPORT_REGISTRY 1
#endif /* __KERNEL__ && CONFIG_PROC_FS && CONFIG_XENO_OPT_NATIVE_REGISTRY */

#if defined(__KERNEL__) || defined(__XENO_SIM__)

#include <nucleus/synch.h>
#include <nucleus/thread.h>

struct rt_object_procnode;

typedef struct rt_object {

    xnholder_t link;
#define link2rtobj(laddr) \
((RT_OBJECT *)(((char *)laddr) - (int)(&((RT_OBJECT *)0)->link)))

    void *objaddr;

    const char *key;	/* !< Hash key. */

    xnsynch_t safesynch; /* !< Safe synchronization object. */

    u_long safelock;	 /* !< Safe lock count. */

    u_long cstamp;	/* !< Creation stamp. */

#if defined(CONFIG_PROC_FS) && defined(__KERNEL__)

    struct rt_object_procnode *pnode; /* !< /proc information class. */

    struct proc_dir_entry *proc; /* !< /proc entry. */

#endif /* CONFIG_PROC_FS && __KERNEL__ */

} RT_OBJECT;

typedef struct rt_hash {

    RT_OBJECT *object;

    struct rt_hash *next;	/* !< Next in h-table */

} RT_HASH;

#ifdef __cplusplus
extern "C" {
#endif

int __registry_pkg_init(void);

void __registry_pkg_cleanup(void);

#if defined(CONFIG_PROC_FS) && defined(__KERNEL__)

#include <linux/proc_fs.h>

#define RT_OBJECT_PROC_RESERVED1 ((struct proc_dir_entry *)1)
#define RT_OBJECT_PROC_RESERVED2 ((struct proc_dir_entry *)2)

typedef ssize_t link_proc_t(char *buf,
			    int count,
			    void *data);

typedef struct rt_object_procnode {

    struct proc_dir_entry *dir;
    const char *type;
    int entries;
    read_proc_t *read_proc;
    write_proc_t *write_proc;
    link_proc_t *link_proc;

} RT_OBJECT_PROCNODE;

#else /* !(CONFIG_PROC_FS && __KERNEL__) */

typedef struct rt_object_procnode { /* Placeholder. */

    const char *type;

} RT_OBJECT_PROCNODE;

#endif /* CONFIG_PROC_FS && __KERNEL__ */

/* Public interface. */

int rt_registry_enter(const char *key,
		      void *objaddr,
		      rt_handle_t *phandle,
		      RT_OBJECT_PROCNODE *pnode);

int rt_registry_bind(const char *key,
		     RTIME timeout,
		     rt_handle_t *phandle);

int rt_registry_remove(rt_handle_t handle);

int rt_registry_remove_safe(rt_handle_t handle,
			    RTIME timeout);

void *rt_registry_get(rt_handle_t handle);

void *rt_registry_fetch(rt_handle_t handle);

u_long rt_registry_put(rt_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* __KERNEL__ || __XENO_SIM__ */

#endif /* !_XENO_REGISTRY_H */

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

#ifndef _XENO_MUTEX_H
#define _XENO_MUTEX_H

#include <nucleus/synch.h>
#include <native/types.h>

struct rt_task;

typedef struct rt_mutex_info {

    int lockcnt;	/* !< Lock nesting level (> 0 means "locked"). */

    int nwaiters;	/* !< Number of pending tasks. */

    char name[XNOBJECT_NAME_LEN]; /* !< Symbolic name. */

} RT_MUTEX_INFO;

typedef struct rt_mutex_placeholder {
    rt_handle_t opaque;
} RT_MUTEX_PLACEHOLDER;

#if defined(__KERNEL__) || defined(__XENO_SIM__)

#define XENO_MUTEX_MAGIC 0x55550505

typedef struct __rt_mutex {

    unsigned magic;   /* !< Magic code - must be first */

    xnsynch_t synch_base; /* !< Base synchronization object. */

    rt_handle_t handle;	/* !< Handle in registry -- zero if unregistered. */

    struct rt_task *owner;	/* !< Current mutex owner. */

    int lockcnt;	/* !< Lock nesting level (> 0 means "locked"). */

    char name[XNOBJECT_NAME_LEN]; /* !< Symbolic name. */

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
    pid_t cpid;			/* !< Creator's pid. */
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */

} RT_MUTEX;

#ifdef __cplusplus
extern "C" {
#endif

int __native_mutex_pkg_init(void);

void __native_mutex_pkg_cleanup(void);

#ifdef __cplusplus
}
#endif

#else /* !(__KERNEL__ || __XENO_SIM__) */

typedef RT_MUTEX_PLACEHOLDER RT_MUTEX;

#ifdef __cplusplus
extern "C" {
#endif

int rt_mutex_bind(RT_MUTEX *mutex,
		  const char *name,
		  RTIME timeout);

static inline int rt_mutex_unbind (RT_MUTEX *mutex)

{
    mutex->opaque = RT_HANDLE_INVALID;
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* __KERNEL__ || __XENO_SIM__ */

#ifdef __cplusplus
extern "C" {
#endif

/* Public interface. */

int rt_mutex_create(RT_MUTEX *mutex,
		    const char *name);

int rt_mutex_delete(RT_MUTEX *mutex);

int rt_mutex_lock(RT_MUTEX *mutex,
		  RTIME timeout);

int rt_mutex_unlock(RT_MUTEX *mutex);

int rt_mutex_inquire(RT_MUTEX *mutex,
		     RT_MUTEX_INFO *info);

#ifdef __cplusplus
}
#endif

#endif /* !_XENO_MUTEX_H */

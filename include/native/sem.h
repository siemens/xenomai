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

#ifndef _XENO_SEM_H
#define _XENO_SEM_H

#include <xenomai/nucleus/synch.h>
#include <xenomai/native/types.h>

/* Creation flags. */
#define S_PRIO  XNSYNCH_PRIO	/* Pend by task priority order. */
#define S_FIFO  XNSYNCH_FIFO	/* Pend by FIFO order. */
#define S_PULSE 0x100		/* Apply pulse mode. */

typedef struct rt_sem_info {

    unsigned long count; /* !< Current semaphore value. */

    int nwaiters;	/* !< Number of pending tasks. */

    char name[XNOBJECT_NAME_LEN]; /* !< Symbolic name. */

} RT_SEM_INFO;

typedef struct rt_sem_placeholder {
    rt_handle_t opaque;
} RT_SEM_PLACEHOLDER;

#if defined(__KERNEL__) || defined(__XENO_SIM__)

#define XENO_SEM_MAGIC 0x55550303

typedef struct rt_sem {

    unsigned magic;   /* !< Magic code - must be first */

    xnsynch_t synch_base; /* !< Base synchronization object. */

    unsigned long count; /* !< Current semaphore value. */

    int mode;		/* !< Creation mode. */

    rt_handle_t handle;	/* !< Handle in registry -- zero if unregistered. */

    char name[XNOBJECT_NAME_LEN]; /* !< Symbolic name. */

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
    pid_t cpid;			/* !< Creator's pid. */
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */

} RT_SEM;

#ifdef __cplusplus
extern "C" {
#endif

int __native_sem_pkg_init(void);

void __native_sem_pkg_cleanup(void);

#ifdef __cplusplus
}
#endif

#else /* !(__KERNEL__ || __XENO_SIM__) */

typedef RT_SEM_PLACEHOLDER RT_SEM;

#ifdef __cplusplus
extern "C" {
#endif

int rt_sem_bind(RT_SEM *sem,
		const char *name,
		RTIME timeout);

static inline int rt_sem_unbind (RT_SEM *sem)

{
    sem->opaque = RT_HANDLE_INVALID;
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

int rt_sem_create(RT_SEM *sem,
		  const char *name,
		  unsigned long icount,
		  int mode);

int rt_sem_delete(RT_SEM *sem);

int rt_sem_p(RT_SEM *sem,
	     RTIME timeout);

int rt_sem_v(RT_SEM *sem);

int rt_sem_broadcast(RT_SEM *sem);

int rt_sem_inquire(RT_SEM *sem,
		   RT_SEM_INFO *info);

#ifdef __cplusplus
}
#endif

#endif /* !_XENO_SEM_H */

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

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#ifndef _COPPERPLATE_LOCK_H
#define _COPPERPLATE_LOCK_H

/*
 * COPPERPLATE_PROTECT/UNPROTECT() should enclose any emulator code
 * prior to holding a lock, or invoking copperplate services (which
 * usually do so), to change the system state. A proper cleanup
 * handler should be pushed prior to acquire such lock.
 *
 * Those macros ensure that cancellation type is switched to deferred
 * mode while the section is traversed, then restored to its original
 * value upon exit.
 *
 * WARNING: copperplate DOES ASSUME that cancellability is deferred
 * for the caller, so you really want to define protected sections as
 * required in the higher interface layers.
 */
struct service {
	int cancel_type;
};

#ifdef CONFIG_XENO_ASYNC_CANCEL

#define COPPERPLATE_PROTECT(__s)						\
	do {								\
		pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED,		\
				      &(__s).cancel_type);		\
	} while (0)

#define COPPERPLATE_UNPROTECT(__s)						\
	do {								\
		pthread_setcanceltype((__s).cancel_type, NULL);		\
	} while (0)

#else  /* !CONFIG_XENO_ASYNC_CANCEL */

#define COPPERPLATE_PROTECT(__s)	do { (void)(__s); } while (0)

#define COPPERPLATE_UNPROTECT(__s)	do { } while (0)

#endif  /* !CONFIG_XENO_ASYNC_CANCEL */

#define push_cleanup_lock(lock)		\
	pthread_cleanup_push((void (*)(void *))pthread_mutex_unlock, (lock))

#define pop_cleanup_lock(lock)		\
	pthread_cleanup_pop(0)

#define __do_lock_nocancel(__lock, __op)			\
	({							\
		int __ret;					\
		__ret = -pthread_mutex_##__op(__lock);		\
		__ret;						\
	})

#define __do_unlock_nocancel(__lock)				\
	({							\
		int __ret;					\
		__ret = -pthread_mutex_unlock(__lock);		\
		__ret;						\
	})

#define __do_lock(__lock, __op)		__do_lock_nocancel(__lock, __op)

#define __do_unlock(__lock, __op)	__do_unlock_nocancel(__lock)

/*
 * Macros to enter/leave critical sections within
 * copperplate. Actually, they are mainly aimed at self-documenting
 * the code, by specifying basic assumption(s) about the code being
 * traversed. In effect, they are currently aliases to the standard
 * pthread_mutex_* API, except for the _safe form.
 *
 * The _nocancel suffix indicates that no cancellation point is
 * traversed by the protected code, therefore we don't need any
 * cleanup handler since we are guaranteed to run in deferred cancel
 * mode after COPPERPLATE_PROTECT().
 *
 * read/write_lock() forms must be enclosed within the scope of a
 * cleanup handler since the protected code may reach cancellation
 * points. push_cleanup_lock() is a simple shorthand to push
 * pthread_mutex_unlock as the cleanup handler.
 */
#define read_lock(__lock)			\
	__do_lock(__lock, lock)

#define read_trylock(__lock)			\
	__do_lock(__lock, trylock)

#define read_lock_nocancel(__lock)		\
	__do_lock_nocancel(__lock, lock)

#define read_trylock_nocancel(__lock)		\
	__do_lock_nocancel(__lock, trylock)

#define read_unlock(__lock)			\
	__do_unlock_nocancel(__lock)

#define write_lock(__lock)			\
	__do_lock(__lock, lock)

#define write_trylock(__lock)			\
	__do_lock(__lock, trylock)

#define write_lock_nocancel(__lock)		\
	__do_lock_nocancel(__lock, lock)

#define write_trylock_nocancel(__lock)		\
	__do_lock_nocancel(__lock, trylock)

#define write_unlock(__lock)			\
	__do_unlock_nocancel(__lock)

#define __do_lock_safe(__lock, __state, __op)				\
	({								\
		int __ret;						\
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &(__state)); \
		__ret = -pthread_mutex_##__op(__lock);			\
		if (__ret)						\
			pthread_setcancelstate(__state, NULL);		\
		__ret;							\
	})

#define __do_unlock_safe(__lock, __state)				\
	({								\
		int __ret;						\
		__ret = -pthread_mutex_unlock(__lock);			\
		pthread_setcancelstate(__state, NULL);			\
		__ret;							\
	})

/*
 * The _safe call form is available when undoing the changes from an
 * update section upon cancellation using a cleanup handler is not an
 * option (e.g. too complex); in this case, cancellation is disabled
 * throughout the section.
 */	

#define write_lock_safe(__lock, __state)	\
	__do_lock_safe(__lock, __state, lock)

#define write_trylock_safe(__lock, __state)	\
	__do_lock_safe(__lock, __state, trylock)

#define write_unlock_safe(__lock, __state)	\
	__do_unlock_safe(__lock, __state)

#endif /* _COPPERPLATE_LOCK_H */

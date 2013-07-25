/*
 * Copyright (C) 2013 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _COBALT_POSIX_EXTENSION_H
#define _COBALT_POSIX_EXTENSION_H

#include <linux/time.h>
#include <cobalt/kernel/shadow.h>

#ifdef CONFIG_XENO_OPT_COBALT_EXTENSION

struct cobalt_thread;
struct cobalt_timer;
struct cobalt_sigpending;
struct cobalt_extref;
struct siginfo;

struct cobalt_extension {
	struct xnpersonality core;
	struct {
		struct cobalt_thread *
		(*timer_init)(struct cobalt_extref *reftimer,
			      const struct sigevent *__restrict__ evp);
		int (*timer_cleanup)(struct cobalt_extref *reftimer);
		int (*signal_deliver)(struct cobalt_extref *refthread,
				      struct siginfo *si,
				      struct cobalt_sigpending *sigp);
		int (*signal_queue)(struct cobalt_extref *refthread,
				    struct cobalt_sigpending *sigp);
		int (*signal_copyinfo)(struct cobalt_extref *refthread,
				       struct siginfo __user *u_si,
				       const struct siginfo *si,
				       int overrun);
	} ops;
};

struct cobalt_extref {
	struct cobalt_extension *extension;
	struct cobalt_thread *owner;
	void *private;
};

static inline void cobalt_set_extref(struct cobalt_extref *ref,
				     struct cobalt_extension *ext,
				     void *priv)
{
	ref->extension = ext;
	ref->private = priv;
	ref->owner = NULL;
}

/**
 * Both macros return non-zero if some thread-level extension code was
 * called, leaving the output value into __ret. Otherwise, the __ret
 * value is undefined.
 */
#define cobalt_initcall_extension(__extfn, __extref, __owner, __ret, __args...) \
	({									\
		int __val = 0;							\
		if ((__owner) && (__owner)->extref.extension) {			\
			(__extref)->extension = (__owner)->extref.extension;	\
			(__extref)->owner = (__owner);				\
			if ((__extref)->extension->ops.__extfn) {		\
				(__ret) = (__extref)->extension->ops.		\
					__extfn(__extref, ##__args );		\
				__val = 1;					\
			}							\
		} else {							\
			(__extref)->extension = NULL;				\
			(__extref)->owner = NULL;				\
		}								\
		__val;								\
	})
		
#define cobalt_call_extension(__extfn, __extref, __ret, __args...)	\
	({								\
		int __val = 0;						\
		if ((__extref)->extension &&				\
		    (__extref)->extension->ops.__extfn) {		\
			(__ret) = (__extref)->extension->ops.		\
				__extfn(__extref, ##__args );		\
			__val = 1;					\
		}							\
		__val;							\
	})
		
#else /* !CONFIG_XENO_OPT_COBALT_EXTENSION */

struct cobalt_extension;

struct cobalt_extref {
};

static inline void cobalt_set_extref(struct cobalt_extref *ref,
				     struct cobalt_extension *ext,
				     void *priv)
{
}

#define cobalt_initcall_extension(__extfn, __extref, __owner, __ret, __args...)	\
	({ (void)(__owner); (void)(__ret); 0; })

#define cobalt_call_extension(__extfn, __extref, __ret, __args...)		\
	({ (void)(__ret); 0; })

#endif /* !CONFIG_XENO_OPT_COBALT_EXTENSION */

#endif /* !_COBALT_POSIX_EXTENSION_H */

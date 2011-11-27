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

#ifndef _COPPERPLATE_DEBUG_H
#define _COPPERPLATE_DEBUG_H

#ifdef __XENO_DEBUG__

#include <stdint.h>
#include <pthread.h>

static inline int bad_pointer(const void *ptr)
{
	return ptr == NULL || ((intptr_t)ptr & (sizeof(intptr_t)-1)) != 0;
}

static inline int must_check(void)
{
	return 1;
}

struct threadobj;

struct error_frame {
	int retval;
	int lineno;
	const char *fn;
	const char *file;
	struct error_frame *next;
};

struct backtrace_data {
	const char *name;
	struct error_frame *inner;
	pthread_mutex_t lock;
	char eundef[16];
};

#ifndef likely
#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)
#endif

#define debug(__fmt, __args...)						\
	do {								\
		struct threadobj *__thobj = threadobj_current();	\
		if (__thobj == NULL ||					\
		    (__thobj->status & THREADOBJ_DEBUG) != 0)		\
			__debug(__thobj, __fmt, ##__args);		\
	} while (0)

#ifdef __cplusplus
extern "C" {
#endif

void backtrace_init_context(struct backtrace_data *btd,
			    const char *name);

void backtrace_destroy_context(struct backtrace_data *btd);

void backtrace_dump(struct backtrace_data *btd);

void backtrace_log(int retval, const char *fn,
		   const char *file, int lineno);

void backtrace_check(void);

void __debug(struct threadobj *thobj, const char *fmt, ...);

char *__get_error_buf(size_t *sizep);

int debug_pkg_init(void);

#ifdef __cplusplus
}
#endif

#define __bt(__exp)						\
	({							\
		typeof(__exp) __ret = (__exp);			\
		if (unlikely(__ret < 0))			\
			backtrace_log((int)__ret, __FUNCTION__,	\
				      __FILE__, __LINE__);	\
		__ret;						\
	})

#else /* !__XENO_DEBUG__ */

static inline int bad_pointer(const void *ptr)
{
	return 0;
}

static inline int must_check(void)
{
	return 0;
}

#define debug(fmt, args...)  do { } while (0)

struct backtrace_data {
};

#define __bt(__exp)			(__exp)

#define backtrace_init_context(btd, name)	\
	do { (void)(btd); (void)(name); } while (0)

#define backtrace_destroy_context(btd)	\
	do { (void)(btd); } while (0)

#define backtrace_dump(btd)		\
	do { (void)(btd); } while (0)

#define backtrace_check()		\
	do { } while (0)
/*
 * XXX: We have no thread-private backtrace context in non-debug mode,
 * so there is a potential race if multiple threads want to write to
 * this buffer. This looks acceptable though, since this is primarily
 * a debug feature, and the race won't damage the system anyway.
 */
#define __get_error_buf(sizep)			\
	({					\
		static char __buf[16];		\
		*(sizep) = sizeof(__buf);	\
		__buf;				\
	})

#define debug_pkg_init()	({ 0; })

#endif /* !__XENO_DEBUG__ */

#endif /* _COPPERPLATE_DEBUG_H */

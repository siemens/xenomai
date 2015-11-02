/*
 * Copyright (C) 2014 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _XENOMAI_SMOKEY_SMOKEY_H
#define _XENOMAI_SMOKEY_SMOKEY_H

#include <stdarg.h>
#include <boilerplate/list.h>
#include <boilerplate/libc.h>
#include <copperplate/clockobj.h>
#include <xenomai/init.h>

#define SMOKEY_INT(__name) {		\
	 .name = # __name,		\
	 .parser = smokey_int,		\
	 .matched = 0,			\
	 }

#define SMOKEY_BOOL(__name) {		\
	 .name = # __name,		\
	 .parser = smokey_bool,		\
	 .matched = 0,			\
	 }

#define SMOKEY_STRING(__name) {		\
	 .name = # __name,		\
	 .parser = smokey_string,	\
	 .matched = 0,			\
	 }

#define SMOKEY_ARGLIST(__args...)  ((struct smokey_arg[]){ __args })

#define SMOKEY_NOARGS  (((struct smokey_arg[]){ { .name = NULL } }))

struct smokey_arg {
	const char *name;
	int (*parser)(const char *s,
		      struct smokey_arg *arg);
	union {
		int n_val;
		char *s_val;
	} u;
	int matched;
};

struct smokey_test {
	const char *name;
	struct smokey_arg *args;
	int nargs;
	const char *description;
	int (*run)(struct smokey_test *t,
		   int argc, char *const argv[]);
	struct {
		int id;
		struct pvholder next;
	} __reserved;
};

#define for_each_smokey_test(__pos)	\
	pvlist_for_each_entry((__pos), &smokey_test_list, __reserved.next)

#define __smokey_arg_count(__args)	\
	(sizeof(__args) / sizeof(__args[0]))

#define smokey_test_plugin(__plugin, __args, __desc)			\
	static int run_ ## __plugin(struct smokey_test *t,		\
				    int argc, char *const argv[]);	\
	static struct smokey_test __plugin = {				\
		.name = #__plugin,					\
		.args = (__args),					\
		.nargs = __smokey_arg_count(__args),			\
		.description = (__desc),				\
		.run = run_ ## __plugin,				\
	};								\
	__early_ctor void smokey_plugin_ ## __plugin(void);		\
	void smokey_plugin_ ## __plugin(void)				\
	{								\
		smokey_register_plugin(&(__plugin));			\
	}

#define SMOKEY_ARG(__plugin, __arg)	   (smokey_lookup_arg(&(__plugin), # __arg))
#define SMOKEY_ARG_ISSET(__plugin, __arg)  (SMOKEY_ARG(__plugin, __arg)->matched)
#define SMOKEY_ARG_INT(__plugin, __arg)	   (SMOKEY_ARG(__plugin, __arg)->u.n_val)
#define SMOKEY_ARG_BOOL(__plugin, __arg)   (!!SMOKEY_ARG_INT(__plugin, __arg))
#define SMOKEY_ARG_STRING(__plugin, __arg) (SMOKEY_ARG(__plugin, __arg)->u.s_val)

#define smokey_check_errno(__expr)					\
	({                                                              \
		int __ret = (__expr);					\
		if (__ret < 0) {					\
			__ret = -errno;					\
			__smokey_warning(__FILE__, __LINE__, "%s: %s",	\
					 #__expr, strerror(errno));	\
		}							\
		__ret;							\
	})

#define smokey_check_status(__expr)					\
	({                                                              \
		int __ret = (__expr);					\
		if (__ret) {						\
			__smokey_warning(__FILE__, __LINE__, "%s: %s",	\
					 #__expr, strerror(__ret));	\
			__ret = -__ret;					\
		}							\
		__ret;							\
	})

#define smokey_assert(__expr)						\
	({                                                              \
		int __ret = (__expr);					\
		if (!__ret) 						\
			__smokey_warning(__FILE__, __LINE__,		\
					 "assertion failed: %s", #__expr); \
		__ret;							\
	})

#define smokey_warning(__fmt, __args...)	\
	__smokey_warning(__FILE__, __LINE__, __fmt, ##__args)

#ifdef __cplusplus
extern "C" {
#endif

void smokey_register_plugin(struct smokey_test *t);

int smokey_int(const char *s, struct smokey_arg *arg);

int smokey_bool(const char *s, struct smokey_arg *arg);

int smokey_string(const char *s, struct smokey_arg *arg);

struct smokey_arg *smokey_lookup_arg(struct smokey_test *t,
				     const char *arg);

int smokey_parse_args(struct smokey_test *t,
		      int argc, char *const argv[]);

void smokey_vatrace(const char *fmt, va_list ap);
  
void smokey_trace(const char *fmt, ...);

void smokey_note(const char *fmt, ...);

void __smokey_warning(const char *file, int lineno,
		      const char *fmt, ...);

#ifdef __cplusplus
}
#endif

extern struct pvlistobj smokey_test_list;

extern int smokey_keep_going;

extern int smokey_verbose_mode;

extern int smokey_on_vm;

#endif /* _XENOMAI_SMOKEY_SMOKEY_H */

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
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <boilerplate/ancillaries.h>
#include <smokey/smokey.h>

int smokey_int(const char *s, struct smokey_arg *arg)
{
	char *name, *p;
	int ret;

	ret = sscanf(s, "%m[_a-z]=%m[^\n]", &name, &p);
	if (ret != 2 || !(isdigit(*p) || *p == '-'))
		return 0;

	ret = !strcmp(name, arg->name);
	if (ret)
		arg->u.n_val = atoi(p);

	free(p);
	free(name);

	return ret;
}

int smokey_bool(const char *s, struct smokey_arg *arg)
{
	int ret;

	ret = smokey_int(s, arg);
	if (ret) {
		arg->u.n_val = !!arg->u.n_val;
		return 1;
	}

	if (strcmp(s, arg->name) == 0) {
		arg->u.n_val = 1;
		return 1;
	}

	return 0;
}

int smokey_string(const char *s, struct smokey_arg *arg)
{
	char *name, *p;
	int ret;

	ret = sscanf(s, "%m[_a-z]=%m[^\n]", &name, &p);
	if (ret != 2)
		return 0;

	ret = !strcmp(name, arg->name);
	if (ret)
		arg->u.s_val = p;
	else
		free(p);

	free(name);

	return ret;
}

int smokey_parse_args(struct smokey_test *t,
		      int argc, char *const argv[])
{
	int matched = 0, n, ac;
	struct smokey_arg *arg;

	for (arg = t->args, ac = 0;
	     arg->name && ac < t->nargs; arg++, ac++) {
		for (n = 1; n < argc; n++) {
			arg->matched = !!arg->parser(argv[n], arg);
			if (arg->matched) {
				matched++;
				break;
			}
		}
	}

	return matched;
}

struct smokey_arg *smokey_lookup_arg(struct smokey_test *t,
				     const char *name)
{
	struct smokey_arg *arg = NULL;
	int ac;

	for (arg = t->args, ac = 0;
	     arg->name && ac < t->nargs; arg++, ac++) {
		if (strcmp(arg->name, name) == 0)
			return arg;
	}

	/* Assume this is fatal. */
	panic("test %s has no argument \"%s\"",
	      t->name, name);
}

void smokey_note(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);

	if (smokey_verbose_mode) {
		__RT(vfprintf(stdout, fmt, ap));
		__RT(fprintf(stdout, "\n"));
	}

	va_end(ap);
}

void smokey_vatrace(const char *fmt, va_list ap)
{
	if (smokey_verbose_mode > 1) {
		__RT(vfprintf(stdout, fmt, ap));
		__RT(fprintf(stdout, "\n"));
	}
}

void smokey_trace(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	smokey_vatrace(fmt, ap);
	va_end(ap);
}

void __smokey_warning(const char *file, int lineno,
		      const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);

	if (smokey_verbose_mode) {
		__RT(fprintf(stderr, "%s:%d, ", basename(file), lineno));
		__RT(vfprintf(stderr, fmt, ap));
		__RT(fprintf(stderr, "\n"));
	}

	va_end(ap);
}

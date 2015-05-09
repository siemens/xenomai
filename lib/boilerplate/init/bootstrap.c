/*
 * Copyright (C) 2013 Philippe Gerum <rpm@xenomai.org>.
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
#include <sys/types.h>
#include <unistd.h>
#include <malloc.h>
#include <string.h>
#include <fcntl.h>
#include <xenomai/init.h>

static int early_argc;

static char *const *early_argv;

int __real_main(int argc, char *const argv[]);

int __wrap_main(int argc, char *const argv[])
__attribute__((alias("xenomai_main"), weak));

int xenomai_main(int argc, char *const argv[])
{
	if (early_argc)
		return __real_main(early_argc, early_argv);
	
	xenomai_init(&argc, &argv);

	return __real_main(argc, argv);
}

__bootstrap_ctor static void xenomai_bootstrap(void)
{
	char *arglist, *argend, *p, **v, *const *argv;
	ssize_t len, ret;
	int fd, n, argc;

	len = 1024;

	for (;;) {
		fd = __STD(open("/proc/self/cmdline", O_RDONLY));
		if (fd < 0)
			return;

		arglist = malloc(len);
		if (arglist == NULL) {
			__STD(close(fd));
			return;
		}

		ret = __STD(read(fd, arglist, len));
		__STD(close(fd));

		if (ret < 0) {
			free(arglist);
			return;
		}

		if (ret < len)
			break;

		free(arglist);
		len <<= 1;
	}

	argend = arglist + ret;
	p = arglist;
	n = 0;
	while (p < argend) {
		n++;
		p += strlen(p) + 1;
	}

	v = malloc((n + 1) * sizeof(char *));
	if (v == NULL) {
		free(arglist);
		return;
	}

	p = arglist;
	n = 0;
	while (p < argend) {
		v[n++] = p;
		p += strlen(p) + 1;
	}

	v[n] = NULL;
	argv = v;
	argc = n;

	xenomai_init(&argc, &argv);
	early_argc = argc;
	early_argv = argv;
}

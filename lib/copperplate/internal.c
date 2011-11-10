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

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <linux/unistd.h>
#include <copperplate/clockobj.h>
#include <copperplate/threadobj.h>
#include "internal.h"

struct coppernode __this_node = {
	.mem_pool = 128 * 1024, /* Default, 128 Kb. */
	.session_label = "anon",
	.no_mlock = 0,
	.no_registry = 0,
	.reset_session = 0,
};

pthread_mutex_t __printlock;

pid_t copperplate_get_tid(void)
{
	/*
	 * XXX: The nucleus maintains a hash table indexed on
	 * task_pid_vnr() values for mapped shadows. This is what
	 * __NR_gettid retrieves as well in Cobalt mode.
	 */
	return syscall(__NR_gettid);
}

#ifdef CONFIG_XENO_COBALT

int copperplate_probe_node(unsigned int id)
{
	/*
	 * XXX: this call does NOT migrate to secondary mode therefore
	 * may be used in time-critical contexts. However, since the
	 * nucleus has to know about a probed thread to find out
	 * whether it exists, copperplate_init() must always be
	 * invoked from a real-time shadow, so that __this_node.id can
	 * be matched.
	 */
	return pthread_probe_np((pid_t)id) == 0;
}

#else /* CONFIG_XENO_MERCURY */

int copperplate_probe_node(unsigned int id)
{
	return kill((pid_t)id, 0) == 0;
}

#endif  /* CONFIG_XENO_MERCURY */

void __printout(struct threadobj *thobj,
		const char *header, const char *fmt, va_list ap)
{
	unsigned long long ms, us, ns;
	struct timespec now, delta;
	FILE *fp = stderr;

	__RT(clock_gettime(CLOCK_COPPERPLATE, &now));
	timespec_sub(&delta, &now, &__init_date);
	ns = delta.tv_sec * 1000000000ULL;
	ns += delta.tv_nsec;
	ms = ns / 1000000ULL;
	us = (ns % 1000000ULL) / 1000ULL;

	push_cleanup_lock(&__printlock);
	write_lock(&__printlock);

	fprintf(fp, "%4d\"%.3d.%.3d| ",
		(int)ms / 1000, (int)ms % 1000, (int)us);

	if (header)
		fputs(header, fp);

	fprintf(fp, "[%s] ", thobj ? threadobj_get_name(thobj) : "main");
	vfprintf(fp, fmt, ap);
	fputc('\n', fp);
	fflush(fp);

	write_unlock(&__printlock);
	pop_cleanup_lock(&__printlock);
}

void warning(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	__printout(threadobj_current(), "WARNING: ", fmt, ap);
	va_end(ap);
}

void panic(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	__printout(threadobj_current(), "BUG: ", fmt, ap);
	va_end(ap);

	exit(1);
}

__attribute__ ((weak))
void error_hook(struct error_frame *ef) /* NULL in non-debug mode */
{
}

const char *dashes = "------------------------------------------------------------------------------";

#define __esym_def(e)	[e] = #e

static const char *__esym_map[] = {
	[0] = "OK",
	__esym_def(EPERM),
	__esym_def(ENOENT),
	__esym_def(ESRCH),
	__esym_def(EINTR),
	__esym_def(EIO),
	__esym_def(ENXIO),
	__esym_def(E2BIG),
	__esym_def(ENOEXEC),
	__esym_def(EBADF),
	__esym_def(ECHILD),
	__esym_def(EAGAIN),
	__esym_def(ENOMEM),
	__esym_def(EACCES),
	__esym_def(EFAULT),
	__esym_def(ENOTBLK),
	__esym_def(EBUSY),
	__esym_def(EEXIST),
	__esym_def(EXDEV),
	__esym_def(ENODEV),
	__esym_def(ENOTDIR),
	__esym_def(EISDIR),
	__esym_def(EINVAL),
	__esym_def(ENFILE),
	__esym_def(EMFILE),
	__esym_def(ENOTTY),
	__esym_def(ETXTBSY),
	__esym_def(EFBIG),
	__esym_def(ENOSPC),
	__esym_def(ESPIPE),
	__esym_def(EROFS),
	__esym_def(EMLINK),
	__esym_def(EPIPE),
	__esym_def(EDOM),
	__esym_def(ERANGE),
	__esym_def(ENOSYS),
	__esym_def(ETIMEDOUT),
	__esym_def(ENOMSG),
	__esym_def(EADDRINUSE),
};

#define __esym_max  (sizeof(__esym_map) / sizeof(__esym_map[0]))

const char *symerror(int errnum)
{
	int v = -errnum;
	size_t ebufsz;
	char *ebuf;

	if (v < 0 || v >= (int)__esym_max || __esym_map[v] == NULL) {
		/* Catch missing codes in the error map. */
		ebuf = __get_error_buf(&ebufsz);
		snprintf(ebuf, ebufsz, "%d?", errnum);
		return ebuf;
	}

	return __esym_map[v];
}

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
#include <sys/syscall.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <malloc.h>
#include "boilerplate/atomic.h"
#include "boilerplate/lock.h"
#include "boilerplate/time.h"
#include "boilerplate/scope.h"
#include "boilerplate/setup.h"
#include "boilerplate/debug.h"
#include "boilerplate/ancillaries.h"
#include "xenomai/init.h"

pthread_mutex_t __printlock;

static struct timespec init_date;

static int init_done;

static void __do_printout(const char *name, const char *header,
			  unsigned int ms, unsigned int us,
			  const char *fmt, va_list ap)
{
	FILE *fp = stderr;

	fprintf(fp, "%4u\"%.3u.%.3u| ", ms / 1000, ms % 1000, us);

	if (header)
		fputs(header, fp);

	fprintf(fp, "[%s] ", name ?: "main");
	vfprintf(fp, fmt, ap);
	fputc('\n', fp);
	fflush(fp);
}

void __printout(const char *name, const char *header,
		const char *fmt, va_list ap)
{
	struct timespec now, delta;
	unsigned long long ns;
	unsigned int ms, us;

	/*
	 * Catch early printouts, when the init sequence is not
	 * completed yet. In such event, we don't care for serializing
	 * output, since we must be running over the main thread
	 * uncontended.
	 */
	if (!init_done) {
		__do_printout(name, header, 0, 0, fmt, ap);
		return;
	}
	
	__RT(clock_gettime(CLOCK_MONOTONIC, &now));
	timespec_sub(&delta, &now, &init_date);
	ns = delta.tv_sec * 1000000000ULL;
	ns += delta.tv_nsec;
	ms = ns / 1000000ULL;
	us = (ns % 1000000ULL) / 1000ULL;
	SIGSAFE_LOCK_ENTRY(&__printlock);
	__do_printout(name, header, ms, us, fmt, ap);
	SIGSAFE_LOCK_EXIT(&__printlock);
}

void __warning(const char *name, const char *fmt, va_list ap)
{
	__printout(name, "WARNING: ", fmt, ap);
}

void __notice(const char *name, const char *fmt, va_list ap)
{
	__printout(name, NULL, fmt, ap);
}

void ___panic(const char *fn, const char *name,
	     const char *fmt, va_list ap)
{
	char *p;

	if (asprintf(&p, "BUG in %s(): ", fn) < 0)
		p = "BUG: ";
	__printout(name, p, fmt, ap);
	exit(1);
}

__weak void error_hook(struct error_frame *ef) /* NULL in non-debug mode */
{
}

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
	__esym_def(EIDRM),
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

void __run_cleanup_block(struct cleanup_block *cb)
{
	__RT(pthread_mutex_unlock(cb->lock));
	cb->handler(cb->arg);
}

void __early_panic(const char *fn, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	___panic(fn, NULL, fmt, ap);
	va_end(ap);
}

void __panic(const char *fn, const char *fmt, ...)
__attribute__((alias("__early_panic"), weak));

void early_warning(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	__warning(NULL, fmt, ap);
	va_end(ap);
}

void warning(const char *fmt, ...)
__attribute__((alias("early_warning"), weak));

void early_notice(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	__notice(NULL, fmt, ap);
	va_end(ap);
}

void notice(const char *fmt, ...)
__attribute__((alias("early_notice"), weak));

char *generate_name(char *buf, const char *radix,
		    struct name_generator *ngen)
{
	int len = ngen->length - 1, tag;

	if (radix && *radix) {
		strncpy(buf, radix, len);
		buf[len] = '\0';
	} else {
		tag = atomic_add_fetch(&ngen->serial, 1);
		snprintf(buf, len, "%s@%d", ngen->radix, tag);
	}

	return buf;
}

#ifdef CONFIG_XENO_PSHARED

/*
 * Client libraries may override these symbols for implementing heap
 * pointer validation in their own context (e.g. copperplate).
 */

__weak int pshared_check(void *heap, void *addr)
{
	return 1;
}

__weak void *__main_heap = NULL;

#endif /* !CONFIG_XENO_PSHARED */

#ifdef CONFIG_XENO_DEBUG

int __check_cancel_type(const char *locktype)
{
	int oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &oldtype);
	if (oldtype == PTHREAD_CANCEL_DEFERRED)
		return 0;

	warning("%s() section is NOT cancel-safe", locktype);
	abort();

	return __bt(-EINVAL);
}

#endif /* CONFIG_XENO_DEBUG */

int get_static_cpu_count(void)
{
	char buf[BUFSIZ];
	int count = 0;
	FILE *fp;

	/*
	 * We want the maximum # of CPU the running kernel was
	 * configured for, not the current online/present/possible
	 * count of CPU devices.
	 */
	fp = fopen("/sys/devices/system/cpu/kernel_max", "r");
	if (fp == NULL)
		return -1;

	if (fgets(buf, sizeof(buf), fp))
		count = atoi(buf);

	fclose(fp);

	return count;
}

pid_t get_thread_pid(void)
{
	return syscall(__NR_gettid);
}

char *lookup_command(const char *cmd)
{
	const char *dirs[] = {
		"/bin",
		"/sbin",
		"/usr/bin",
		"/usr/sbin",
	};
	char *path;
	int n, ret;

	for (n = 0; n < sizeof(dirs) / sizeof(dirs[0]); n++) {
		ret = asprintf(&path, "%s/%s", dirs[n], cmd);
		if (ret < 0)
			return NULL;
		if (access(path, X_OK) == 0)
			return path;
		free(path);
	}

	return NULL;
}

const char *config_strings[] = {
#include "config-dump.h"
	NULL,
};

void __boilerplate_init(void)
{
	__RT(clock_gettime(CLOCK_MONOTONIC, &init_date));
	__RT(pthread_mutex_init(&__printlock, NULL));
	debug_init();
	init_done = 1;
}

static int boilerplate_init(void)
{
	pthread_atfork(NULL, NULL, __boilerplate_init);
	__boilerplate_init();

	return 0;
}

static struct setup_descriptor boilerplate_interface = {
	.name = "boilerplate",
	.init = boilerplate_init,
};

boilerplate_setup_call(boilerplate_interface);

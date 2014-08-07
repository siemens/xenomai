/*
 * Copyright (C) 2009 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
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
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <cobalt/uapi/kernel/heap.h>
#include <asm/xenomai/syscall.h>
#include "current.h"
#include "sem_heap.h"
#include "internal.h"

#define PRIVATE 0
#define SHARED 1

struct xnvdso *vdso;

unsigned long cobalt_sem_heap[2] = { 0, 0 };

static pthread_once_t init_private_heap = PTHREAD_ONCE_INIT;
static struct xnheap_desc private_hdesc;

void *cobalt_map_heap(struct xnheap_desc *hd)
{
	int fd, ret;
	void *addr;

	fd = open(XNHEAP_DEV_NAME, O_RDWR, 0);
	if (fd < 0) {
		report_error("cannot open %s: %s", XNHEAP_DEV_NAME,
			     strerror(errno));
		return MAP_FAILED;
	}

	ret = ioctl(fd, 0, hd->handle);
	if (ret) {
		report_error("failed association with %s: %s",
			     XNHEAP_DEV_NAME, strerror(errno));
		return MAP_FAILED;
	}

	addr = __STD(mmap(NULL, hd->size, PROT_READ|PROT_WRITE,
			  MAP_SHARED, fd, hd->area));

	close(fd);

	return addr;
}

static void *map_sem_heap(unsigned int shared)
{
	struct xnheap_desc global_hdesc, *hdesc;
	int ret;

	hdesc = shared ? &global_hdesc : &private_hdesc;
	ret = XENOMAI_SYSCALL2(sc_nucleus_heap_info, hdesc, shared);
	if (ret < 0) {
		report_error("cannot locate %s heap: %s",
			     shared ? "shared" : "private",
			     strerror(-ret));
		return MAP_FAILED;
	}

	return cobalt_map_heap(hdesc);
}

static void unmap_on_fork(void)
{
	void *addr;

	/*
	 * Remapping the private heap must be done after the process
	 * has re-attached to the Cobalt core, in order to reinstate a
	 * proper private heap, Otherwise the global heap would be
	 * used instead, leading to unwanted effects.
	 *
	 * On machines without an MMU, there is no such thing as fork.
	 *
	 * We replace former mappings with an invalid one, to detect
	 * any spuriously late access from the fastsync code.
	 */
	addr = __STD(mmap((void *)cobalt_sem_heap[PRIVATE],
			  private_hdesc.size, PROT_NONE,
			  MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0));

	if (addr != (void *)cobalt_sem_heap[PRIVATE])
		munmap((void *)cobalt_sem_heap[PRIVATE], private_hdesc.size);

	cobalt_sem_heap[PRIVATE] = 0UL;
	init_private_heap = PTHREAD_ONCE_INIT;
}

static void cobalt_init_vdso(void)
{
	struct xnsysinfo sysinfo;
	int ret;

	ret = XENOMAI_SYSCALL1(sc_nucleus_info, &sysinfo);
	if (ret < 0) {
		report_error("sysinfo failed: %s", strerror(-ret));
		exit(EXIT_FAILURE);
	}

	vdso = (struct xnvdso *)(cobalt_sem_heap[SHARED] + sysinfo.vdso);
}

/* Will be called once at library loading time, and when re-binding
   after a fork */
static void cobalt_init_private_heap(void)
{
	cobalt_sem_heap[PRIVATE] = (unsigned long)map_sem_heap(PRIVATE);
	if (cobalt_sem_heap[PRIVATE] == (unsigned long)MAP_FAILED) {
		report_error("cannot map private heap: %s",
			     strerror(errno));
		exit(EXIT_FAILURE);
	}
}

/* Will be called only once, at library loading time. */
static void cobalt_init_rest_once(void)
{
	pthread_atfork(NULL, NULL, unmap_on_fork);

	cobalt_sem_heap[SHARED] = (unsigned long)map_sem_heap(SHARED);
	if (cobalt_sem_heap[SHARED] == (unsigned long)MAP_FAILED) {
		report_error("cannot map shared heap: %s",
			     strerror(errno));
		exit(EXIT_FAILURE);
	}

	cobalt_init_vdso();
}

void cobalt_init_sem_heaps(void)
{
	static pthread_once_t init_rest_once = PTHREAD_ONCE_INIT;

	pthread_once(&init_private_heap, &cobalt_init_private_heap);
	pthread_once(&init_rest_once, &cobalt_init_rest_once);
}

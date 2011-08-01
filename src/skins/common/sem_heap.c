#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <nucleus/vdso.h>
#include <nucleus/heap.h>
#include <asm/xenomai/syscall.h>
#include <asm-generic/bits/current.h>
#include "sem_heap.h"

#define PRIVATE 0
#define SHARED 1

struct xnvdso *nkvdso;

unsigned long xeno_sem_heap[2] = { 0, 0 };

static pthread_once_t init_private_heap = PTHREAD_ONCE_INIT;
static struct xnheap_desc private_hdesc;

void *xeno_map_heap(struct xnheap_desc *hd)
{
	int fd, ret;
	void *addr;

	fd = open(XNHEAP_DEV_NAME, O_RDWR, 0);
	if (fd < 0) {
		perror("Xenomai: open");
		return MAP_FAILED;
	}

	ret = ioctl(fd, 0, hd->handle);
	if (ret) {
		perror("Xenomai: ioctl");
		return MAP_FAILED;
	}

	addr = mmap(NULL, hd->size, PROT_READ|PROT_WRITE,
		    MAP_SHARED, fd, hd->area);

	close(fd);

	return addr;
}

static void *map_sem_heap(unsigned int shared)
{
	struct xnheap_desc global_hdesc, *hdesc;
	int ret;

	hdesc = shared ? &global_hdesc : &private_hdesc;
	ret = XENOMAI_SYSCALL2(__xn_sys_heap_info, hdesc, shared);
	if (ret < 0) {
		errno = -ret;
		perror("Xenomai: sys_heap_info");
		return MAP_FAILED;
	}

	return xeno_map_heap(hdesc);
}

static void unmap_on_fork(void)
{
	/*
	   Remapping the private heap must be done after the process has been
	   bound again, in order for it to have a new private heap,
	   Otherwise the global heap would be used instead, which
	   leads to unwanted effects.

	   On machines without an MMU, there is no such thing as fork.

	   As a protection against access to the heaps by the fastsync
	   code, we set up an inaccessible mapping where the heap was, so
	   that access to these addresses will cause a segmentation
	   fault.
	*/
#if defined(CONFIG_XENO_FASTSYNCH)
	void *addr = mmap((void *)xeno_sem_heap[PRIVATE],
			  private_hdesc.size, PROT_NONE,
			  MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if (addr != (void *)xeno_sem_heap[PRIVATE])
#endif /* CONFIG_XENO_FASTSYNCH */
		munmap((void *)xeno_sem_heap[PRIVATE], private_hdesc.size);
	xeno_sem_heap[PRIVATE] = 0UL;
	init_private_heap = PTHREAD_ONCE_INIT;
}

static void xeno_init_vdso(void)
{
	xnsysinfo_t sysinfo;
	int err;

	err = XENOMAI_SYSCALL2(__xn_sys_info, 0, &sysinfo);
	if (err < 0) {
		errno = -err;
		perror("Xenomai: sys_info failed");
		exit(EXIT_FAILURE);
	}

	nkvdso = (struct xnvdso *)(xeno_sem_heap[SHARED] + sysinfo.vdso);
}

/* Will be called once at library loading time, and when re-binding
   after a fork */
static void xeno_init_private_heap(void)
{
	xeno_sem_heap[PRIVATE] = (unsigned long)map_sem_heap(PRIVATE);
	if (xeno_sem_heap[PRIVATE] == (unsigned long)MAP_FAILED) {
		perror("Xenomai: mmap local sem heap");
		exit(EXIT_FAILURE);
	}
}

/* Will be called only once, at library loading time. */
static void xeno_init_rest_once(void)
{
	pthread_atfork(NULL, NULL, unmap_on_fork);

	xeno_sem_heap[SHARED] = (unsigned long)map_sem_heap(SHARED);
	if (xeno_sem_heap[SHARED] == (unsigned long)MAP_FAILED) {
		perror("Xenomai: mmap global sem heap");
		exit(EXIT_FAILURE);
	}

	xeno_init_vdso();
}

void xeno_init_sem_heaps(void)
{
	static pthread_once_t init_rest_once = PTHREAD_ONCE_INIT;

	pthread_once(&init_private_heap, &xeno_init_private_heap);
	pthread_once(&init_rest_once, &xeno_init_rest_once);
}

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

unsigned long xeno_sem_heap[2] = { 0, 0 };

struct xnvdso *nkvdso;

void *xeno_map_heap(unsigned long handle, unsigned int size)
{
	int fd, ret;
	void *addr;

	fd = open(XNHEAP_DEV_NAME, O_RDWR, 0);
	if (fd < 0) {
		perror("Xenomai: open");
		return MAP_FAILED;
	}

	ret = ioctl(fd, 0, handle);
	if (ret) {
		perror("Xenomai: ioctl");
		return MAP_FAILED;
	}

	addr = mmap(NULL, size, PROT_READ|PROT_WRITE,
		    MAP_SHARED, fd, 0L);

	close(fd);

	return addr;
}

static void *map_sem_heap(unsigned int shared)
{
	struct xnheap_desc hinfo;
	int ret;

	ret = XENOMAI_SYSCALL2(__xn_sys_sem_heap, &hinfo, shared);
	if (ret < 0) {
		errno = -ret;
		perror("Xenomai: sys_sem_heap");
		return MAP_FAILED;
	}

	return xeno_map_heap(hinfo.handle, hinfo.size);
}

static void unmap_sem_heap(unsigned long heap_addr, unsigned int shared)
{
	struct xnheap_desc hinfo;
	int ret;

	ret = XENOMAI_SYSCALL2(__xn_sys_sem_heap, &hinfo, shared);
	if (ret < 0) {
		errno = -ret;
		perror("Xenomai: unmap sem_heap");
		return;
	}

	munmap((void *)heap_addr, hinfo.size);
}

static void remap_on_fork(void)
{
	unmap_sem_heap(xeno_sem_heap[0], 0);

	xeno_sem_heap[0] = (unsigned long) map_sem_heap(0);
	if (xeno_sem_heap[0] == (unsigned long) MAP_FAILED) {
		perror("Xenomai: mmap local sem heap");
		exit(EXIT_FAILURE);
	}
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

	nkvdso = (struct xnvdso *)(xeno_sem_heap[1] + sysinfo.vdso);
	if (!xnvdso_test_feature(XNVDSO_FEAT_DROP_U_MODE))
		xeno_current_warn_old();
}

static void xeno_init_sem_heaps_inner(void)
{
	xeno_sem_heap[0] = (unsigned long) map_sem_heap(0);
	if (xeno_sem_heap[0] == (unsigned long) MAP_FAILED) {
		perror("Xenomai: mmap local sem heap");
		exit(EXIT_FAILURE);
	}
	pthread_atfork(NULL, NULL, remap_on_fork);

	xeno_sem_heap[1] = (unsigned long) map_sem_heap(1);
	if (xeno_sem_heap[1] == (unsigned long) MAP_FAILED) {
		perror("Xenomai: mmap global sem heap");
		exit(EXIT_FAILURE);
	}

	xeno_init_vdso();
}

void xeno_init_sem_heaps(void)
{
	static pthread_once_t init_sem_heaps_once = PTHREAD_ONCE_INIT;
	pthread_once(&init_sem_heaps_once, &xeno_init_sem_heaps_inner);
}

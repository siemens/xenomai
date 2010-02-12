#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <asm/xenomai/syscall.h>

#include "sem_heap.h"

__attribute__ ((weak))
unsigned long xeno_sem_heap[2] = { 0, 0 };

static void *map_sem_heap(unsigned shared)
{
	struct heap_info {
		void *addr;
		unsigned size;
	} hinfo;
	int fd, err;

	fd = open("/dev/rtheap", O_RDWR, 0);
	if (fd < 0) {
		fprintf(stderr, "Xenomai: open: %m\n");
		return MAP_FAILED;
	}

	err = XENOMAI_SYSCALL2(__xn_sys_sem_heap, &hinfo, shared);
	if (err < 0) {
		fprintf(stderr, "Xenomai: sys_sem_heap: %m\n");
		return MAP_FAILED;
	}

	err = ioctl(fd, 0, hinfo.addr);
	if (err < 0) {
		fprintf(stderr, "Xenomai: ioctl: %m\n");
		return MAP_FAILED;
	}

	hinfo.addr = mmap(NULL, hinfo.size,
			  PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);

	return hinfo.addr;
}

static void unmap_sem_heap(unsigned long heap_addr, unsigned shared)
{
	struct heap_info {
		void *addr;
		unsigned size;
	} hinfo;
	int err;

	err = XENOMAI_SYSCALL2(__xn_sys_sem_heap, &hinfo, shared);
	if (err < 0) {
		fprintf(stderr, "Xenomai: sys_sem_heap: %m\n");
		return;
	}

	munmap((void *) heap_addr, hinfo.size);
}

static void remap_on_fork(void)
{
	unmap_sem_heap(xeno_sem_heap[0], 0);

	xeno_sem_heap[0] = (unsigned long) map_sem_heap(0);
	if (xeno_sem_heap[0] == (unsigned long) MAP_FAILED) {
		perror("Xenomai: mmap(local sem heap)");
		exit(EXIT_FAILURE);
	}
}

void xeno_init_sem_heaps(void)
{
	if (xeno_sem_heap[0])
		return;

	xeno_sem_heap[0] = (unsigned long) map_sem_heap(0);
	if (xeno_sem_heap[0] == (unsigned long) MAP_FAILED) {
		perror("Xenomai: mmap(local sem heap)");
		exit(EXIT_FAILURE);
	}
	pthread_atfork(NULL, NULL, remap_on_fork);

	xeno_sem_heap[1] = (unsigned long) map_sem_heap(1);
	if (xeno_sem_heap[1] == (unsigned long) MAP_FAILED) {
		perror("Xenomai: mmap(global sem heap)");
		exit(EXIT_FAILURE);
	}
}

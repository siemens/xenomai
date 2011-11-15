#ifndef _XENO_ASM_ARM_BIND_H
#define _XENO_ASM_ARM_BIND_H

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>

#include <asm-generic/xenomai/bits/bind.h>
#include <asm/xenomai/syscall.h>

__attribute__((weak)) struct __xn_tscinfo __xn_tscinfo = {
  type: -1
};

static inline void xeno_arm_features_check(struct xnfeatinfo *finfo)
{
	unsigned long phys_addr;
	unsigned page_size;
	int err, fd;
	void *addr;

	if (__xn_tscinfo.type != -1)
		return;

	err = XENOMAI_SYSCALL2(__xn_sys_arch,
			       XENOMAI_SYSARCH_TSCINFO, &__xn_tscinfo);
	if (err)
		goto error;

	fd = open("/dev/mem", O_RDONLY | O_SYNC);
	if (fd == -1) {
		perror("Xenomai init: open(/dev/mem)");
		exit(EXIT_FAILURE);
	}

	page_size = sysconf(_SC_PAGESIZE);

	switch(__xn_tscinfo.type) {
	case __XN_TSC_TYPE_FREERUNNING:
	case __XN_TSC_TYPE_FREERUNNING_COUNTDOWN:
	case __XN_TSC_TYPE_FREERUNNING_FAST_WRAP:
	case __XN_TSC_TYPE_DECREMENTER:
		goto domap;

	case __XN_TSC_TYPE_NONE:
	  error:
		fprintf(stderr, "Xenomai: Your board/configuration does not"
			" allow tsc emulation in user-space: %d\n", err);
		exit(EXIT_FAILURE);
		break;

	default:
		fprintf(stderr,
			"Xenomai: kernel/user tsc emulation mismatch.\n");
		exit(EXIT_FAILURE);
	}

  domap:
	phys_addr = (unsigned long) __xn_tscinfo.counter;

	addr = mmap(NULL, page_size, PROT_READ, MAP_SHARED,
		    fd, phys_addr & ~(page_size - 1));
	if (addr == MAP_FAILED) {
		perror("Xenomai init: mmap(/dev/mem)");
		exit(EXIT_FAILURE);
	}

	__xn_tscinfo.counter =
		((volatile unsigned *)
		 ((char *) addr + (phys_addr & (page_size - 1))));

	if (close(fd)) {
		perror("Xenomai init: close(/dev/mem)");
		exit(EXIT_FAILURE);
	}
}
#define xeno_arch_features_check(finfo) xeno_arm_features_check(finfo)

#endif /* _XENO_ASM_ARM_BIND_H */

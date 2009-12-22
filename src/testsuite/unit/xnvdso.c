/*
 * VDSO feature set testcase
 * by Wolfgang Mauerer <wolfgang.mauerer@siemens.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <asm/xenomai/syscall.h>
#include <nucleus/xnvdso.h>

extern unsigned long xeno_sem_heap[2];

int main(int argc, char **argv)
{
	int err;
	xnsysinfo_t sysinfo;
	struct xnvdso *xnvdso;
	unsigned long long test_features;

	if (argc != 2) {
		printf("No specific feature(s) given, using XNVDSO_FEATURE\n");
		test_features = XNVDSO_FEATURES;
	} else {
		test_features = strtoull(argv[1], NULL, 0);
	}

	if (!xeno_sem_heap[1]) {
		fprintf(stderr, "Could not determine position of the "
			"global semaphore heap\n");
		return 1;
	}

	/* The muxid is irrelevant for this test as long as it's valid */
	err = XENOMAI_SYSCALL2(__xn_sys_info, 1, &sysinfo);
	if (err < 0) {
		fprintf(stderr, "sys_sys_info failed: %d\n", err);
		return 1;
	}

	printf("Address of the global semaphore heap: 0x%lx\n",
	       xeno_sem_heap[1]);
	printf("Offset of xnvdso: %lu\n", sysinfo.xnvdso_off);

	xnvdso = (struct xnvdso *)(xeno_sem_heap[1] + sysinfo.xnvdso_off);
	printf("Contents of the features flag: %llu\n", xnvdso->features);

	if (xnvdso->features == test_features)
		return 0;

	fprintf(stderr, "error: xnvdso->features != %llu\n", test_features);
	return 1;
}

/*
 * VDSO feature set testcase
 * by Wolfgang Mauerer <wolfgang.mauerer@siemens.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <asm/xenomai/syscall.h>
#include <nucleus/vdso.h>

extern unsigned long xeno_sem_heap[2];

int main(int argc, char **argv)
{
	unsigned long long test_features;

	if (argc != 2) {
		printf("No specific feature(s) given, using XNVDSO_FEATURES\n");
		test_features = XNVDSO_FEATURES;
	} else {
		test_features = strtoull(argv[1], NULL, 0);
	}

	if (!xeno_sem_heap[1]) {
		fprintf(stderr, "Could not determine position of the "
			"global semaphore heap\n");
		return 1;
	}

	printf("Contents of the features flag: %llu\n", nkvdso->features);

	if (nkvdso->features == test_features)
		return 0;

	fprintf(stderr, "error: nkvdso->features != %llu\n", test_features);
	return 1;
}

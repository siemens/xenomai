/*
 * VDSO feature set testcase
 * by Wolfgang Mauerer <wolfgang.mauerer@siemens.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <nocore/atomic.h>
#include <cobalt/uapi/kernel/vdso.h>

extern unsigned long cobalt_sem_heap[2];

extern struct xnvdso *vdso;

int main(int argc, char **argv)
{
	if (cobalt_sem_heap[1] == 0) {
		fprintf(stderr, "Could not determine position of the "
			"global semaphore heap\n");
		return 1;
	}

	printf("Contents of the features flag: %llu\n", vdso->features);

	return 0;
}

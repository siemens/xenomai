/*
 * VDSO feature set testcase
 * by Wolfgang Mauerer <wolfgang.mauerer@siemens.com>
 */
#include <stdio.h>
#include <time.h>
#include <boilerplate/atomic.h>
#include <cobalt/uapi/kernel/vdso.h>
#include <smokey/smokey.h>

smokey_test_plugin(vdso_access,
		   SMOKEY_NOARGS,
		   "Check VDSO access."
);

extern unsigned long cobalt_sem_heap[2];

extern struct xnvdso *vdso;

int run_vdso_access(struct smokey_test *t, int argc, char *const argv[])
{
	if (cobalt_sem_heap[1] == 0) {
		warning("could not determine position of the VDSO segment");
		return 1;
	}

	printf("VDSO: features detected: %llx\n", vdso->features);

	return 0;
}

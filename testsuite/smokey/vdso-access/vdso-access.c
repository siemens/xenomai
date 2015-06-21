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

extern void *cobalt_umm_shared;

extern struct xnvdso *cobalt_vdso;

int run_vdso_access(struct smokey_test *t, int argc, char *const argv[])
{
	if (cobalt_umm_shared == NULL) {
		warning("could not determine position of the VDSO segment");
		return 1;
	}

	smokey_trace("VDSO: features detected: %llx",
		     (long long)cobalt_vdso->features);

	return 0;
}

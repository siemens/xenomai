/*
 * Functional testing of RTDM services.
 *
 * Copyright (C) 2010 Jan Kiszka <jan.kiszka@web.de>.
 *
 * Released under the terms of GPLv2.
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <rtdm/testing.h>
#include <smokey/smokey.h>

smokey_test_plugin(rtdm,
		   SMOKEY_NOARGS,
		   "Check core interface to RTDM services."
);

#define NS_PER_MS (1000000)

static inline unsigned long long timer_get_tsc(void)
{
	return clockobj_get_tsc();
}

static inline unsigned long long timer_tsc2ns(unsigned long long tsc)
{
	return clockobj_tsc_to_ns(tsc);
}

static void check_inner(const char *fn, int line, const char *msg,
			int status, int expected)
{
	if (status == expected)
		return;

	fprintf(stderr, "FAILED %s:%d: %s returned %d instead of %d - %s\n",
		fn, line, msg, status, expected, strerror(-status));
	exit(EXIT_FAILURE);
}

#define check(msg, status, expected) ({					\
	int __status = status;						\
	check_inner(__func__, __LINE__, msg,			\
		    __status < 0 ? -errno : __status, expected);	\
	__status;							\
})

#define check_no_error(msg, status) ({					\
	int __status = status;						\
	check_inner(__func__, __LINE__, msg,			\
		    __status < 0 ? -errno : 0, 0);			\
	__status;							\
})

static void check_sleep_inner(const char *fn, int line,
			      const char *msg, unsigned long long start)
{
	unsigned long long diff = timer_tsc2ns(timer_get_tsc() - start);

	if (diff < 300 * NS_PER_MS) {
		fprintf(stderr, "FAILED %s:%d: %s waited only %Ld.%06u ms\n",
			fn, line, msg, diff / 1000000,
			(unsigned)(diff % 1000000));
		exit(EXIT_FAILURE);
	}
}
#define check_sleep(msg, start) \
	check_sleep_inner(__func__, __LINE__, msg, start)

static const char *devname = "/dev/rtdm/rtdm0";
static const char *devname2 = "/dev/rtdm/rtdm1";

static int run_rtdm(struct smokey_test *t, int argc, char *const argv[])
{
	unsigned long long start;
	int dev, dev2, status;

	status = system("modprobe -q xeno_rtdmtest");
	if (status < 0 || WEXITSTATUS(status))
		return -ENOSYS;

	if (access(devname, 0) < 0 && errno == ENOENT)
		return -ENOSYS;

	smokey_trace("Setup");
	dev = check_no_error("open", open(devname, O_RDWR));

	smokey_trace("Exclusive open");
	check("open", open(devname, O_RDWR), -EBUSY);

	smokey_trace("Successive open");
	dev2 = check("open", open(devname2, O_RDWR), dev + 1);
	check("close", close(dev2), 0);

	smokey_trace("Defer close by pending reference");
	check("ioctl", ioctl(dev, RTTST_RTIOC_RTDM_DEFER_CLOSE,
			     RTTST_RTDM_DEFER_CLOSE_CONTEXT), 0);
	start = timer_get_tsc();
	check("close", close(dev), 0);
	check("open", open(devname, O_RDWR), -EBUSY);
	dev2 = check("open", open(devname2, O_RDWR), dev);
	check("close", close(dev2), 0);
	usleep(301000);
	dev = check("open", open(devname, O_RDWR), dev);

	smokey_trace("Normal close");
	check("ioctl", ioctl(dev, RTTST_RTIOC_RTDM_DEFER_CLOSE,
			     RTTST_RTDM_NORMAL_CLOSE), 0);
	check("close", close(dev), 0);
	dev = check("open", open(devname, O_RDWR), dev);

	smokey_trace("Deferred module unload");
	check("ioctl", ioctl(dev, RTTST_RTIOC_RTDM_DEFER_CLOSE,
			     RTTST_RTDM_DEFER_CLOSE_CONTEXT), 0);
	start = timer_get_tsc();
	check("close", close(dev), 0);
	check("rmmod", system("rmmod xeno_rtdmtest"), 0);
	check_sleep("rmmod", start);

	return 0;
}

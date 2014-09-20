/*
 * Copyright (C) 2010 Jan Kiszka <jan.kiszka@web.de>.
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/module.h>

#include <rtdm/driver.h>
#include <rtdm/testing.h>

MODULE_DESCRIPTION("RTDM test helper module");
MODULE_AUTHOR("Jan Kiszka <jan.kiszka@web.de>");
MODULE_VERSION("0.1.0");
MODULE_LICENSE("GPL");

struct rtdm_test_context {
	rtdm_timer_t close_timer;
	unsigned long close_counter;
	unsigned long close_deferral;
};

static void close_timer_proc(rtdm_timer_t *timer)
{
	struct rtdm_test_context *ctx =
		container_of(timer, struct rtdm_test_context, close_timer);

	if (ctx->close_counter != 1)
		printk(KERN_ERR
		       "rtdmtest: %s: close_counter is %lu, should be 1!\n",
		       __FUNCTION__, ctx->close_counter);

	rtdm_fd_unlock(rtdm_private_to_fd(ctx));
}

static int rtdm_test_open(struct rtdm_fd *fd, int oflags)
{
	struct rtdm_test_context *ctx = rtdm_fd_to_private(fd);

	rtdm_timer_init(&ctx->close_timer, close_timer_proc,
			"rtdm close test");
	ctx->close_counter = 0;
	ctx->close_deferral = RTTST_RTDM_NORMAL_CLOSE;

	return 0;
}

static void rtdm_test_close(struct rtdm_fd *fd)
{
	struct rtdm_test_context *ctx = rtdm_fd_to_private(fd);

	ctx->close_counter++;

	switch (ctx->close_deferral) {
	case RTTST_RTDM_DEFER_CLOSE_CONTEXT:
		if (ctx->close_counter != 2) {
			printk(KERN_ERR
			       "rtdmtest: %s: close_counter is %lu, "
			       "should be 2!\n",
			       __FUNCTION__, ctx->close_counter);
			return;
		}
		break;
	}

	rtdm_timer_destroy(&ctx->close_timer);
}

static int
rtdm_test_ioctl(struct rtdm_fd *fd, unsigned int request, void __user *arg)
{
	struct rtdm_test_context *ctx = rtdm_fd_to_private(fd);
	int err = 0;

	switch (request) {
	case RTTST_RTIOC_RTDM_DEFER_CLOSE:
		ctx->close_deferral = (unsigned long)arg;
		if (ctx->close_deferral == RTTST_RTDM_DEFER_CLOSE_CONTEXT) {
			++ctx->close_counter;
			rtdm_fd_lock(fd);
			rtdm_timer_start(&ctx->close_timer, 300000000ULL, 0,
					RTDM_TIMERMODE_RELATIVE);
		}
		break;

	default:
		err = -ENOTTY;
	}

	return err;
}

static struct rtdm_driver rtdmtest_driver = {
	.profile_info		= RTDM_PROFILE_INFO(rtdmtest,
						    RTDM_CLASS_TESTING,
						    RTDM_SUBCLASS_RTDMTEST,
						    RTTST_PROFILE_VER),
	.device_flags		= RTDM_NAMED_DEVICE | RTDM_EXCLUSIVE,
	.device_count		= 2,
	.context_size		= sizeof(struct rtdm_test_context),
	.ops = {
		.open		= rtdm_test_open,
		.close		= rtdm_test_close,
		.ioctl_rt	= rtdm_test_ioctl,
		.ioctl_nrt	= rtdm_test_ioctl,
	},
};

static struct rtdm_device device[2] = {
	[0 ... 1] = {
		.driver = &rtdmtest_driver,
		.label = "rtdm%d",
	}
};

static int __init __rtdm_test_init(void)
{
	int i, ret;

	if (!realtime_core_enabled())
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(device); i++) {
		ret = rtdm_dev_register(device + i);
		if (ret)
			goto fail;
	}

	return 0;
fail:
	while (i-- > 0)
		rtdm_dev_unregister(device + i);

	return ret;
}

static void __exit __rtdm_test_exit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(device); i++)
		rtdm_dev_unregister(device + i);
}

module_init(__rtdm_test_init);
module_exit(__rtdm_test_exit);

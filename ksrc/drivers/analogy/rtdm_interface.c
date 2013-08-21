/**
 * @file
 * Analogy for Linux, user interface (open, read, write, ioctl, proc)
 *
 * Copyright (C) 1997-2000 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2008 Alexis Berlemont <alexis.berlemont@free.fr>
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

#ifndef DOXYGEN_CPP

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>

#include <rtdm/rtdm_driver.h>

#include <analogy/context.h>
#include <analogy/ioctl.h>
#include <analogy/device.h>
#include <analogy/transfer.h>

int (*a4l_ioctl_functions[NB_IOCTL_FUNCTIONS]) (a4l_cxt_t *, void *) = {
	a4l_ioctl_devcfg,
	a4l_ioctl_devinfo,
	a4l_ioctl_subdinfo,
	a4l_ioctl_chaninfo,
	a4l_ioctl_rnginfo,
	a4l_ioctl_cmd,
	a4l_ioctl_cancel,
	a4l_ioctl_insnlist,
	a4l_ioctl_insn,
	a4l_ioctl_bufcfg,
	a4l_ioctl_bufinfo,
	a4l_ioctl_poll,
	a4l_ioctl_mmap, 
	a4l_ioctl_nbchaninfo, 
	a4l_ioctl_nbrnginfo,
	a4l_ioctl_bufcfg2,
	a4l_ioctl_bufinfo2
};

#ifdef CONFIG_PROC_FS
struct proc_dir_entry *a4l_proc_root;

static int a4l_proc_devs_open(struct inode *inode, struct file *file)
{
	return single_open(file, a4l_rdproc_devs, NULL);
}

static const struct file_operations a4l_proc_devs_ops = {
	.open		= a4l_proc_devs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int a4l_proc_drvs_open(struct inode *inode, struct file *file)
{
	return single_open(file, a4l_rdproc_drvs, NULL);
}

static const struct file_operations a4l_proc_drvs_ops = {
	.open		= a4l_proc_drvs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

int a4l_init_proc(void)
{
	int ret = 0;
	struct proc_dir_entry *entry;

	/* Creates the global directory */
	a4l_proc_root = proc_mkdir("analogy", NULL);
	if (a4l_proc_root == NULL) {
		__a4l_err("a4l_proc_init: "
			  "failed to create /proc/analogy\n");
		return -ENOMEM;
	}

	/* Creates the devices related file */
	entry = proc_create("devices", 0444, a4l_proc_root,
			    &a4l_proc_devs_ops);
	if (entry == NULL) {
		__a4l_err("a4l_proc_init: "
			  "failed to create /proc/analogy/devices\n");
		ret = -ENOMEM;
		goto err_proc_init;
	}
	wrap_proc_dir_entry_owner(entry);

	/* Creates the drivers related file */
	entry = proc_create("drivers", 0444, a4l_proc_root,
			    &a4l_proc_drvs_ops);
	if (entry == NULL) {
		__a4l_err("a4l_proc_init: "
			  "failed to create /proc/analogy/drivers\n");
		ret = -ENOMEM;
		goto err_proc_init;
	}
	wrap_proc_dir_entry_owner(entry);

	return 0;

err_proc_init:
	remove_proc_entry("devices", a4l_proc_root);
	remove_proc_entry("analogy", NULL);
	return ret;
}

void a4l_cleanup_proc(void)
{
	remove_proc_entry("drivers", a4l_proc_root);
	remove_proc_entry("devices", a4l_proc_root);
	remove_proc_entry("analogy", NULL);
}

#else /* !CONFIG_PROC_FS */

#define a4l_init_proc() 0
#define a4l_cleanup_proc()

#endif /* CONFIG_PROC_FS */

int a4l_open(struct rtdm_dev_context *context,
	     rtdm_user_info_t * user_info, int flags)
{
	a4l_cxt_t *cxt = (a4l_cxt_t *)rtdm_context_to_private(context);

	/* Get a pointer on the selected device
	   (thanks to minor index) */
	a4l_set_dev(cxt);

	/* Initialize the buffer structure */
	cxt->buffer = rtdm_malloc(sizeof(a4l_buf_t));
	a4l_init_buffer(cxt->buffer);

	/* Allocate the asynchronous buffer
	   NOTE: it should be interesting to allocate the buffer only
	   on demand especially if the system is short of memory */
	if (cxt->dev->transfer.default_bufsize)
		a4l_alloc_buffer(cxt->buffer,
				 cxt->dev->transfer.default_bufsize);

	return 0;
}

int a4l_close(struct rtdm_dev_context *context, rtdm_user_info_t * user_info)
{
	int err;
	a4l_cxt_t *cxt = (a4l_cxt_t *)rtdm_context_to_private(context);

	/* Cancel the maybe occuring asynchronous transfer */
	err = a4l_cancel_buffer(cxt);
	if (err < 0) {
		__a4l_err("close: unable to stop the asynchronous transfer\n");
		return err;
	}

	/* Free the buffer which was linked with this context and... */
	a4l_free_buffer(cxt->buffer);

	/* ...free the other buffer resources (sync) and... */
	a4l_cleanup_buffer(cxt->buffer);

	/* ...free the structure */
	rtdm_free(cxt->buffer);

	return 0;
}

ssize_t a4l_read(struct rtdm_dev_context * context,
		 rtdm_user_info_t * user_info, void *buf, size_t nbytes)
{
	a4l_cxt_t *cxt = (a4l_cxt_t *)rtdm_context_to_private(context);

	/* Jump into the RT domain if possible */
	if (!rtdm_in_rt_context() && rtdm_rt_capable(user_info))
		return -ENOSYS;

	if (nbytes == 0)
		return 0;

	cxt->user_info = user_info;

	return a4l_read_buffer(cxt, buf, nbytes);
}

ssize_t a4l_write(struct rtdm_dev_context * context,
		  rtdm_user_info_t *user_info, const void *buf, size_t nbytes)
{
	a4l_cxt_t *cxt = (a4l_cxt_t *)rtdm_context_to_private(context);

	/* Jump into the RT domain if possible */
	if (!rtdm_in_rt_context() && rtdm_rt_capable(user_info))
		return -ENOSYS;

	if (nbytes == 0)
		return 0;

	cxt->user_info = user_info;

	return a4l_write_buffer(cxt, buf, nbytes);
}

int a4l_ioctl(struct rtdm_dev_context *context,
	      rtdm_user_info_t *user_info, unsigned int request, void *arg)
{
	a4l_cxt_t *cxt = (a4l_cxt_t *)rtdm_context_to_private(context);

	cxt->user_info = user_info;

	return a4l_ioctl_functions[_IOC_NR(request)] (cxt, arg);
}

int a4l_rt_select(struct rtdm_dev_context *context,
		  rtdm_selector_t *selector,
		  enum rtdm_selecttype type, unsigned fd_index)
{
	a4l_cxt_t *cxt = (a4l_cxt_t *)rtdm_context_to_private(context);

	return a4l_select(cxt, selector, type, fd_index);
}

static struct rtdm_device rtdm_devs[A4L_NB_DEVICES] =
{[0 ... A4L_NB_DEVICES - 1] = {
		.struct_version =	RTDM_DEVICE_STRUCT_VER,
		.device_flags =		RTDM_NAMED_DEVICE,
		.context_size =		sizeof(struct a4l_device_context),
		.device_name = 		"",

		.open_nrt =		a4l_open,

		.ops = {
			.ioctl_rt =	a4l_ioctl,
			.read_rt =	a4l_read,
			.write_rt =	a4l_write,

			.close_nrt =	a4l_close,
			.ioctl_nrt =	a4l_ioctl,
			.read_nrt =	a4l_read,
			.write_nrt =	a4l_write,

			.select_bind =	a4l_rt_select,
		},

		.device_class =		RTDM_CLASS_EXPERIMENTAL,
		.device_sub_class =	RTDM_SUBCLASS_ANALOGY,
		.driver_name =		"rtdm_analogy",
		.driver_version =	RTDM_DRIVER_VER(1, 0, 0),
		.peripheral_name =	"Analogy",
		.provider_name =	"Alexis Berlemont",
	}
};

int a4l_register(void)
{
	int i, ret = 0;

	for (i = 0; i < A4L_NB_DEVICES && ret == 0; i++) {

		/* Sets the device name through which
		   user process can access the Analogy layer */
		snprintf(rtdm_devs[i].device_name,
			 RTDM_MAX_DEVNAME_LEN, "analogy%d", i);
		rtdm_devs[i].proc_name = rtdm_devs[i].device_name;

		/* To keep things simple, the RTDM device ID
		   is the Analogy device index */
		rtdm_devs[i].device_id = i;

		ret = rtdm_dev_register(&(rtdm_devs[i]));
	}

	return ret;
}

void a4l_unregister(void)
{
	int i;
	for (i = 0; i < A4L_NB_DEVICES; i++)
		rtdm_dev_unregister(&(rtdm_devs[i]), 1000);
}

MODULE_DESCRIPTION("Analogy");
MODULE_LICENSE("GPL");

static int __init a4l_init(void)
{
	int ret;

	/* Initializes the devices */
	a4l_init_devs();

	/* Initializes Analogy time management */
	a4l_init_time();

	/* Registers RTDM / fops interface */
	ret = a4l_register();
	if (ret != 0) {
		a4l_unregister();
		goto out_a4l_init;
	}

	/* Initializes Analogy proc layer */
	ret = a4l_init_proc();

out_a4l_init:
	return ret;
}

static void __exit a4l_cleanup(void)
{
	/* Removes Analogy proc files */
	a4l_cleanup_proc();

	/* Unregisters RTDM / fops interface */
	a4l_unregister();
}

module_init(a4l_init);
module_exit(a4l_cleanup);

#endif /* !DOXYGEN_CPP */

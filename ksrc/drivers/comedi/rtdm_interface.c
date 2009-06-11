/**
 * @file
 * Comedi for RTDM, user interface (open, read, write, ioctl, proc)
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

#include <comedi/context.h>
#include <comedi/ioctl.h>
#include <comedi/device.h>

int (*comedi_ioctl_functions[NB_IOCTL_FUNCTIONS]) (comedi_cxt_t *, void *) = {
comedi_ioctl_devcfg,
	    comedi_ioctl_devinfo,
	    comedi_ioctl_subdinfo,
	    comedi_ioctl_chaninfo,
	    comedi_ioctl_rnginfo,
	    comedi_ioctl_cmd,
	    comedi_ioctl_cancel,
	    comedi_ioctl_insnlist,
	    comedi_ioctl_insn,
	    comedi_ioctl_bufcfg,
	    comedi_ioctl_bufinfo,
	    comedi_ioctl_poll,
	    comedi_ioctl_mmap, comedi_ioctl_nbchaninfo, comedi_ioctl_nbrnginfo};

#ifdef CONFIG_PROC_FS
struct proc_dir_entry *comedi_proc_root;

int comedi_init_proc(void)
{
	int ret = 0;
	struct proc_dir_entry *entry;

	/* Creates the global directory */
	comedi_proc_root = create_proc_entry("comedi", S_IFDIR, 0);
	if (comedi_proc_root == NULL) {
		comedi_logerr
		    ("comedi_proc_init: failed to create /proc/comedi\n");
		return -ENOMEM;
	}

	/* Creates the devices related file */
	entry = create_proc_entry("devices", 0444, comedi_proc_root);
	if (entry == NULL) {
		comedi_logerr
		    ("comedi_proc_init: failed to create /proc/comedi/devices\n");
		ret = -ENOMEM;
		goto err_proc_init;
	}

	entry->nlink = 1;
	entry->data = NULL;
	entry->write_proc = NULL;
	entry->read_proc = comedi_rdproc_devs;
	wrap_proc_dir_entry_owner(enrty);

	/* Creates the drivers related file */
	entry = create_proc_entry("drivers", 0444, comedi_proc_root);
	if (entry == NULL) {
		comedi_logerr
		    ("comedi_proc_init: failed to create /proc/comedi/drivers\n");
		ret = -ENOMEM;
		goto err_proc_init;
	}

	entry->nlink = 1;
	entry->data = NULL;
	entry->write_proc = NULL;
	entry->read_proc = comedi_rdproc_drvs;
	wrap_proc_dir_entry_owner(enrty);

	return 0;

      err_proc_init:
	remove_proc_entry("devices", comedi_proc_root);
	remove_proc_entry("comedi", NULL);
	return ret;
}

void comedi_cleanup_proc(void)
{
	remove_proc_entry("drivers", comedi_proc_root);
	remove_proc_entry("devices", comedi_proc_root);
	remove_proc_entry("comedi", NULL);
}

#else /* !CONFIG_PROC_FS */

#define comedi_init_proc() 0
#define comedi_cleanup_proc()

#endif /* CONFIG_PROC_FS */

int comedi_rt_open(struct rtdm_dev_context *context,
		   rtdm_user_info_t * user_info, int flags)
{
	comedi_cxt_t cxt;

	comedi_init_cxt(context, user_info, &cxt);
	comedi_set_dev(&cxt);
	comedi_loginfo("comedi_rt_open: minor=%d\n", comedi_get_minor(&cxt));

	return 0;
}

int comedi_rt_close(struct rtdm_dev_context *context,
		    rtdm_user_info_t * user_info)
{
	comedi_cxt_t cxt;

	comedi_init_cxt(context, user_info, &cxt);
	comedi_set_dev(&cxt);
	comedi_loginfo("comedi_rt_close: minor=%d\n", comedi_get_minor(&cxt));

	return 0;
}

ssize_t comedi_rt_read(struct rtdm_dev_context * context,
		       rtdm_user_info_t * user_info, void *buf, size_t nbytes)
{
	comedi_cxt_t cxt;
	comedi_dev_t *dev;

	comedi_init_cxt(context, user_info, &cxt);
	comedi_set_dev(&cxt);
	dev = comedi_get_dev(&cxt);

	comedi_loginfo("comedi_rt_read: minor=%d\n", comedi_get_minor(&cxt));

	if (nbytes == 0)
		return 0;

	return comedi_read(&cxt, buf, nbytes);
}

ssize_t comedi_rt_write(struct rtdm_dev_context * context,
			rtdm_user_info_t * user_info, const void *buf,
			size_t nbytes)
{
	comedi_cxt_t cxt;
	comedi_dev_t *dev;

	comedi_init_cxt(context, user_info, &cxt);
	comedi_set_dev(&cxt);
	dev = comedi_get_dev(&cxt);

	comedi_loginfo("comedi_rt_write: minor=%d\n", comedi_get_minor(&cxt));

	if (nbytes == 0)
		return 0;

	return comedi_write(&cxt, buf, nbytes);
}

int comedi_rt_ioctl(struct rtdm_dev_context *context,
		    rtdm_user_info_t * user_info,
		    unsigned int request, void *arg)
{
	comedi_cxt_t cxt;

	comedi_init_cxt(context, user_info, &cxt);
	comedi_set_dev(&cxt);
	comedi_loginfo("comedi_rt_ioctl: minor=%d\n", comedi_get_minor(&cxt));

	return comedi_ioctl_functions[_IOC_NR(request)] (&cxt, arg);
}

int comedi_rt_select(struct rtdm_dev_context *context,
		     rtdm_selector_t *selector,
		     enum rtdm_selecttype type, unsigned fd_index)
{
	comedi_cxt_t cxt;

	/* The user_info argument is not available, fortunately it is
	   not critical as no copy_from_user / copy_to_user are to be
	   called */
	comedi_init_cxt(context, NULL, &cxt);
	comedi_set_dev(&cxt);
	comedi_loginfo("comedi_rt_select: minor=%d\n", comedi_get_minor(&cxt));

	return comedi_select(&cxt, selector, type, fd_index);
}

struct comedi_dummy_context {
	int nouse;
};

static struct rtdm_device rtdm_devs[COMEDI_NB_DEVICES] =
{[0 ... COMEDI_NB_DEVICES - 1] = {
	struct_version:	    RTDM_DEVICE_STRUCT_VER,
	device_flags:	    RTDM_NAMED_DEVICE,
	context_size:	    sizeof(struct
				   comedi_dummy_context),
	device_name:	    "",

	open_rt:		    comedi_rt_open,
	open_nrt:	            comedi_rt_open,

	ops:		    {
		close_rt:     comedi_rt_close,
		ioctl_rt:     comedi_rt_ioctl,
		read_rt:	    comedi_rt_read,
		write_rt:     comedi_rt_write,

		close_nrt:    comedi_rt_close,
		ioctl_nrt:    comedi_rt_ioctl,
		read_nrt:     comedi_rt_read,
		write_nrt:    comedi_rt_write,
		      
		select_bind:  comedi_rt_select,
		},

	device_class:	    RTDM_CLASS_EXPERIMENTAL,
	device_sub_class:    RTDM_SUBCLASS_COMEDI,
	driver_name:	    "rtdm_comedi",
	driver_version:	    RTDM_DRIVER_VER(0, 0,
					    2),
	peripheral_name:	    "Comedi",
	provider_name:	    "Alexis Berlemont",
	}
};

int comedi_register(void)
{
	int i, ret = 0;

	for (i = 0; i < COMEDI_NB_DEVICES && ret == 0; i++) {

		/* Sets the device name through which 
		   user process can access the Comedi layer */
		snprintf(rtdm_devs[i].device_name,
			 RTDM_MAX_DEVNAME_LEN, "comedi%d", i);
		rtdm_devs[i].proc_name = rtdm_devs[i].device_name;

		/* To keep things simple, the RTDM device ID 
		   is the Comedi device index */
		rtdm_devs[i].device_id = i;

		ret = rtdm_dev_register(&(rtdm_devs[i]));
	}

	return ret;
}

void comedi_unregister(void)
{
	int i;
	for (i = 0; i < COMEDI_NB_DEVICES; i++)
		rtdm_dev_unregister(&(rtdm_devs[i]), 1000);
}

MODULE_DESCRIPTION("Comedi4RTDM");
MODULE_LICENSE("GPL");

static int __init comedi_init(void)
{
	int ret;

	/* Initializes the devices */
	comedi_init_devs();

	/* Initializes Comedi time management */
	comedi_init_time();

	/* Registers RTDM / fops interface */
	ret = comedi_register();
	if (ret != 0) {
		comedi_unregister();
		goto out_comedi_init;
	}

	/* Initializes Comedi proc layer */
	ret = comedi_init_proc();

      out_comedi_init:
	return ret;
}

static void __exit comedi_cleanup(void)
{
	/* Removes Comedi proc files */
	comedi_cleanup_proc();

	/* Unregisters RTDM / fops interface */
	comedi_unregister();
}

module_init(comedi_init);
module_exit(comedi_cleanup);

#endif /* !DOXYGEN_CPP */

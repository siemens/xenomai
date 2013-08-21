/**
 * @file
 * Analogy for Linux, device related features
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
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/string.h>

#include <analogy/context.h>
#include <analogy/device.h>

#include "proc.h"

static a4l_dev_t a4l_devs[A4L_NB_DEVICES];

/* --- Device tab management functions --- */

void a4l_init_devs(void)
{
	int i;
	memset(a4l_devs, 0, A4L_NB_DEVICES * sizeof(a4l_dev_t));
	for (i = 0; i < A4L_NB_DEVICES; i++) {
		a4l_lock_init(&a4l_devs[i].lock);
		a4l_devs[i].transfer.irq_desc.irq = A4L_IRQ_UNUSED;
	}
}

int a4l_check_cleanup_devs(void)
{
	int i, ret = 0;

	for (i = 0; i < A4L_NB_DEVICES && ret == 0; i++)
		if (test_bit(A4L_DEV_ATTACHED_NR, &a4l_devs[i].flags))
			ret = -EBUSY;

	return ret;
}

void a4l_set_dev(a4l_cxt_t *cxt)
{
	/* Retrieve the minor index */
	const int minor = a4l_get_minor(cxt);
	/* Fill the dev fields accordingly */
	cxt->dev = &(a4l_devs[minor]);
}

/* --- Device tab proc section --- */

#ifdef CONFIG_PROC_FS

int a4l_rdproc_devs(struct seq_file *p, void *data)
{
	int i;

	seq_printf(p, "--  Analogy devices --\n\n");
	seq_printf(p, "| idx | status | driver\n");

	for (i = 0; i < A4L_NB_DEVICES; i++) {
		char *status, *name;

		/* Gets the device's state */
		if (a4l_devs[i].flags == 0) {
			status = "Unused";
			name = "No driver";
		} else if (test_bit(A4L_DEV_ATTACHED_NR, &a4l_devs[i].flags)) {
			status = "Linked";
			name = a4l_devs[i].driver->board_name;
		} else {
			status = "Broken";
			name = "Unknown";
		}

		seq_printf(p, "|  %02d | %s | %s\n", i, status, name);
	}
	return 0;
}

static int a4l_proc_transfer_open(struct inode *inode, struct file *file)
{
	return single_open(file, a4l_rdproc_transfer, PDE_DATA(inode));
}

static const struct file_operations a4l_proc_transfer_ops = {
	.open		= a4l_proc_transfer_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

int a4l_proc_attach(a4l_cxt_t * cxt)
{
	int ret = 0;
	a4l_dev_t *dev = a4l_get_dev(cxt);
	struct proc_dir_entry *entry;
	char *entry_name, *p;

	/* Allocate the buffer for the file name */
	entry_name = rtdm_malloc(A4L_NAMELEN + 4);
	if ((p = entry_name) == NULL) {
		__a4l_err("a4l_proc_attach: failed to allocate buffer\n");
		return -ENOMEM;
	}

	/* Create the proc file name */
	p += sprintf(p, "%02d-", a4l_get_minor(cxt));
	strncpy(p, dev->driver->board_name, A4L_NAMELEN);

	/* Create the proc entry */
	entry = proc_create_data(entry_name, 0444, a4l_proc_root,
				 &a4l_proc_transfer_ops, &dev->transfer);
	if (entry == NULL) {
		__a4l_err("a4l_proc_attach: "
			  "failed to create /proc/analogy/%s\n",
			  entry_name);
		ret = -ENOMEM;
		goto out_setup_proc_transfer;
	}

      out_setup_proc_transfer:
	/* Free the file name buffer */
	rtdm_free(entry_name);

	return ret;
}

void a4l_proc_detach(a4l_cxt_t * cxt)
{
	char *entry_name, *p;
	a4l_dev_t *dev = a4l_get_dev(cxt);

	/* Allocate the buffer for the file name */
	entry_name = rtdm_malloc(A4L_NAMELEN + 4);
	if ((p = entry_name) == NULL) {
		__a4l_err("a4l_proc_detach: "
			  "failed to allocate filename buffer\n");
		return;
	}

	/* Build the name */
	p += sprintf(p, "%02d-", a4l_get_minor(cxt));
	strncpy(p, dev->driver->board_name, A4L_NAMELEN);

	/* Remove the proc file */
	remove_proc_entry(entry_name, a4l_proc_root);

	/* Free the temporary buffer */
	rtdm_free(entry_name);
}

#else /* !CONFIG_PROC_FS */

int a4l_proc_attach(a4l_cxt_t * cxt)
{
	return 0;
}

void a4l_proc_detach(a4l_cxt_t * cxt)
{
}

#endif /* CONFIG_PROC_FS */

/* --- Attach / detach section --- */

int a4l_fill_lnkdesc(a4l_cxt_t * cxt,
		     a4l_lnkdesc_t * link_arg, void *arg)
{
	int ret;
	char *tmpname = NULL;
	void *tmpopts = NULL;

	ret = rtdm_safe_copy_from_user(cxt->user_info,
				       link_arg, arg, sizeof(a4l_lnkdesc_t));
	if (ret != 0) {
		__a4l_err("a4l_fill_lnkdesc: "
			  "call1(copy_from_user) failed\n");
		goto out_get_lnkdesc;
	}

	if (link_arg->bname_size != 0 && link_arg->bname != NULL) {
		tmpname = rtdm_malloc(link_arg->bname_size + 1);
		if (tmpname == NULL) {
			__a4l_err("a4l_fill_lnkdesc: "
				  "call1(alloc) failed\n");
			ret = -ENOMEM;
			goto out_get_lnkdesc;
		}
		tmpname[link_arg->bname_size] = 0;

		ret = rtdm_safe_copy_from_user(cxt->user_info,
					       tmpname,
					       link_arg->bname,
					       link_arg->bname_size);
		if (ret != 0) {
			__a4l_err("a4l_fill_lnkdesc: "
				  "call2(copy_from_user) failed\n");
			goto out_get_lnkdesc;
		}
	} else {
		__a4l_err("a4l_fill_lnkdesc: board name missing\n");
		ret = -EINVAL;
		goto out_get_lnkdesc;
	}

	if (link_arg->opts_size != 0 && link_arg->opts != NULL) {
		tmpopts = rtdm_malloc(link_arg->opts_size);

		if (tmpopts == NULL) {
			__a4l_err("a4l_fill_lnkdesc: "
				  "call2(alloc) failed\n");
			ret = -ENOMEM;
			goto out_get_lnkdesc;
		}

		ret = rtdm_safe_copy_from_user(cxt->user_info,
					       tmpopts,
					       link_arg->opts,
					       link_arg->opts_size);
		if (ret != 0) {
			__a4l_err("a4l_fill_lnkdesc: "
				  "call3(copy_from_user) failed\n");
			goto out_get_lnkdesc;
		}
	}

	link_arg->bname = tmpname;
	link_arg->opts = tmpopts;

      out_get_lnkdesc:

	if (tmpname == NULL) {
		link_arg->bname = NULL;
		link_arg->bname_size = 0;
	}

	if (tmpopts == NULL) {
		link_arg->opts = NULL;
		link_arg->opts_size = 0;
	}

	return ret;
}

void a4l_free_lnkdesc(a4l_cxt_t * cxt, a4l_lnkdesc_t * link_arg)
{
	if (link_arg->bname != NULL)
		rtdm_free(link_arg->bname);

	if (link_arg->opts != NULL)
		rtdm_free(link_arg->opts);
}

int a4l_assign_driver(a4l_cxt_t * cxt,
			 a4l_drv_t * drv, a4l_lnkdesc_t * link_arg)
{
	int ret = 0;
	a4l_dev_t *dev = a4l_get_dev(cxt);

	dev->driver = drv;

	if (drv->privdata_size == 0)
		__a4l_dbg(1, core_dbg,
			  "a4l_assign_driver: warning! "
			  "the field priv will not be usable\n");
	else {

		INIT_LIST_HEAD(&dev->subdvsq);

		dev->priv = rtdm_malloc(drv->privdata_size);
		if (dev->priv == NULL && drv->privdata_size != 0) {
			__a4l_err("a4l_assign_driver: "
				  "call(alloc) failed\n");
			ret = -ENOMEM;
			goto out_assign_driver;
		}

		/* Initialize the private data even if it not our role
		   (the driver should do it), that may prevent hard to
		   find bugs */
		memset(dev->priv, 0, drv->privdata_size);
	}

	if ((ret = drv->attach(dev, link_arg)) != 0)
		__a4l_err("a4l_assign_driver: "
			  "call(drv->attach) failed (ret=%d)\n",
		     ret);

out_assign_driver:

	/* Increments module's count */
	if (ret == 0 && (!try_module_get(drv->owner))) {
		__a4l_err("a4l_assign_driver: "
			  "driver's owner field wrongly set\n");
		ret = -ENODEV;
	}

	if (ret != 0 && dev->priv != NULL) {
		rtdm_free(dev->priv);
		dev->driver = NULL;
	}

	return ret;
}

int a4l_release_driver(a4l_cxt_t * cxt)
{
	int ret = 0;
	a4l_dev_t *dev = a4l_get_dev(cxt);

	if ((ret = dev->driver->detach(dev)) != 0)
		goto out_release_driver;

	/* Decrease module's count
	   so as to allow module unloading */
	module_put(dev->driver->owner);

	/* In case, the driver developer did not free the subdevices */
	while (&dev->subdvsq != dev->subdvsq.next) {
		struct list_head *this = dev->subdvsq.next;
		a4l_subd_t *tmp = list_entry(this, a4l_subd_t, list);

		list_del(this);
		rtdm_free(tmp);
	}

	/* Free the private field */
	rtdm_free(dev->priv);
	dev->driver = NULL;

out_release_driver:
	return ret;
}

int a4l_device_attach(a4l_cxt_t * cxt, void *arg)
{
	int ret = 0;
	a4l_lnkdesc_t link_arg;
	a4l_drv_t *drv = NULL;

	if ((ret = a4l_fill_lnkdesc(cxt, &link_arg, arg)) != 0)
		goto out_attach;

	if ((ret = a4l_lct_drv(link_arg.bname, &drv)) != 0) {
		__a4l_err("a4l_device_attach: "
			  "cannot find board name %s\n", link_arg.bname);
		goto out_attach;
	}

	if ((ret = a4l_assign_driver(cxt, drv, &link_arg)) != 0)
		goto out_attach;

      out_attach:
	a4l_free_lnkdesc(cxt, &link_arg);
	return ret;
}

int a4l_device_detach(a4l_cxt_t * cxt)
{
	a4l_dev_t *dev = a4l_get_dev(cxt);

	if (dev->driver == NULL) {
		__a4l_err("a4l_device_detach: "
			  "incoherent state, driver not reachable\n");
		return -ENXIO;
	}

	return a4l_release_driver(cxt);
}

/* --- IOCTL / FOPS functions --- */

int a4l_ioctl_devcfg(a4l_cxt_t * cxt, void *arg)
{
	int ret = 0;

	if (rtdm_in_rt_context())
		return -ENOSYS;

	if (arg == NULL) {
		/* Basic checking */
		if (!test_bit(A4L_DEV_ATTACHED_NR, &(a4l_get_dev(cxt)->flags))) {
			__a4l_err("a4l_ioctl_devcfg: "
				  "free device, no driver to detach\n");
			return -EINVAL;
		}
		/* Pre-cleanup of the transfer structure, we ensure
		   that nothing is busy */
		if ((ret = a4l_precleanup_transfer(cxt)) != 0)
			return ret;
		/* Remove the related proc file */
		a4l_proc_detach(cxt);
		/* Free the transfer structure and its related data */
		if ((ret = a4l_cleanup_transfer(cxt)) != 0)
			return ret;
		/* Free the device and the driver from each other */
		if ((ret = a4l_device_detach(cxt)) == 0)
			clear_bit(A4L_DEV_ATTACHED_NR,
				  &(a4l_get_dev(cxt)->flags));
	} else {
		/* Basic checking */
		if (test_bit
		    (A4L_DEV_ATTACHED_NR, &(a4l_get_dev(cxt)->flags))) {
			__a4l_err("a4l_ioctl_devcfg: "
				  "linked device, cannot attach more driver\n");
			return -EINVAL;
		}
		/* Pre-initialization of the transfer structure */
		a4l_presetup_transfer(cxt);
		/* Link the device with the driver */
		if ((ret = a4l_device_attach(cxt, arg)) != 0)
			return ret;
		/* Create the transfer structure and
		   the related proc file */
		if ((ret = a4l_setup_transfer(cxt)) != 0 ||
		    (ret = a4l_proc_attach(cxt)) != 0)
			a4l_device_detach(cxt);
		else
			set_bit(A4L_DEV_ATTACHED_NR,
				&(a4l_get_dev(cxt)->flags));
	}

	return ret;
}

int a4l_ioctl_devinfo(a4l_cxt_t * cxt, void *arg)
{
	a4l_dvinfo_t info;
	a4l_dev_t *dev = a4l_get_dev(cxt);

	memset(&info, 0, sizeof(a4l_dvinfo_t));

	if (test_bit(A4L_DEV_ATTACHED_NR, &dev->flags)) {
		int len = (strlen(dev->driver->board_name) > A4L_NAMELEN) ?
		    A4L_NAMELEN : strlen(dev->driver->board_name);

		memcpy(info.board_name, dev->driver->board_name, len);
		info.nb_subd = dev->transfer.nb_subd;
		/* TODO: for API compatibility issue, find the first
		   read subdevice and write subdevice */
	}

	if (rtdm_safe_copy_to_user(cxt->user_info,
				   arg, &info, sizeof(a4l_dvinfo_t)) != 0)
		return -EFAULT;

	return 0;
}

#endif /* !DOXYGEN_CPP */

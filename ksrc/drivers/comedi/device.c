/**
 * @file
 * Comedi for RTDM, device related features
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

#include <comedi/context.h>
#include <comedi/device.h>

#include "proc.h"

static comedi_dev_t comedi_devs[COMEDI_NB_DEVICES];

/* --- Device tab management functions --- */

void comedi_init_devs(void)
{
	int i;
	memset(comedi_devs, 0, COMEDI_NB_DEVICES * sizeof(comedi_dev_t));
	for (i = 0; i < COMEDI_NB_DEVICES; i++)
		comedi_lock_init(&comedi_devs[i].lock);
}

int comedi_check_cleanup_devs(void)
{
	int i, ret = 0;

	for (i = 0; i < COMEDI_NB_DEVICES && ret == 0; i++)
		if (test_bit(COMEDI_DEV_ATTACHED, &comedi_devs[i].flags))
			ret = -EBUSY;

	return ret;
}

void comedi_set_dev(comedi_cxt_t * cxt)
{
	cxt->dev = &(comedi_devs[comedi_get_minor(cxt)]);
}

/* --- Device tab proc section --- */

#ifdef CONFIG_PROC_FS

int comedi_rdproc_devs(char *page,
		       char **start, off_t off, int count, int *eof, void *data)
{
	int i, len = 0;
	char *p = page;

	p += sprintf(p, "--  Comedi devices --\n\n");
	p += sprintf(p, "| idx | status | driver\n");

	for (i = 0; i < COMEDI_NB_DEVICES; i++) {
		char *status, *name;

		/* Gets the device's state */
		if (comedi_devs[i].flags == 0) {
			status = "Unused";
			name = "No driver";
		} else if (test_bit(COMEDI_DEV_ATTACHED, &comedi_devs[i].flags)) {
			status = "Linked";
			name = comedi_devs[i].driver->board_name;
		} else {
			status = "Broken";
			name = "Unknown";
		}

		p += sprintf(p, "|  %02d | %s | %s\n", i, status, name);
	}

	/* Handles any proc-file reading way */
	len = p - page - off;
	/* If the requested size is greater than we provide,
	   the read operation is over */
	if (len <= off + count)
		*eof = 1;
	/* In case the read operation is performed in many steps,
	   the start pointer must be redefined */
	*start = page + off;
	/* If the requested size is lower than we provide,
	   the read operation will be done in more than one step */
	if (len > count)
		len = count;
	/* In case the offset is not correct (too high) */
	if (len < 0)
		len = 0;

	return len;
}

int comedi_proc_attach(comedi_cxt_t * cxt)
{
	int ret = 0;
	comedi_dev_t *dev = comedi_get_dev(cxt);
	struct proc_dir_entry *entry;
	char *entry_name, *p;

	/* Allocates the buffer for the file name */
	entry_name = comedi_kmalloc(COMEDI_NAMELEN + 4);
	if ((p = entry_name) == NULL) {
		comedi_logerr
		    ("comedi_proc_attach: failed to allocate buffer\n");
		return -ENOMEM;
	}

	/* Creates the proc file name */
	p += sprintf(p, "%02d-", comedi_get_minor(cxt));
	strncpy(p, dev->driver->board_name, COMEDI_NAMELEN);

	/* Creates the proc entry */
	entry = create_proc_entry(entry_name, 0444, comedi_proc_root);
	if (entry == NULL) {
		comedi_logerr
		    ("comedi_proc_attach: failed to create /proc/comedi/%s\n",
		     entry_name);
		ret = -ENOMEM;
		goto out_setup_proc_transfer;
	}

	entry->nlink = 1;
	entry->data = dev->transfer;
	entry->write_proc = NULL;
	entry->read_proc = comedi_rdproc_transfer;
	wrap_proc_dir_entry_owner(entry);

      out_setup_proc_transfer:
	/* Frees the file name buffer */
	comedi_kfree(entry_name);

	return ret;
}

void comedi_proc_detach(comedi_cxt_t * cxt)
{
	char *entry_name, *p;
	comedi_dev_t *dev = comedi_get_dev(cxt);

	/* Allocates the buffer for the file name */
	entry_name = comedi_kmalloc(COMEDI_NAMELEN + 4);
	if ((p = entry_name) == NULL) {
		comedi_logerr
		    ("comedi_proc_detach: failed to allocate filename buffer\n");
		return;
	}

	/* Builds the name */
	p += sprintf(p, "%02d-", comedi_get_minor(cxt));
	strncpy(p, dev->driver->board_name, COMEDI_NAMELEN);

	/* Removes the proc file */
	remove_proc_entry(entry_name, comedi_proc_root);

	/* Frees the temporary buffer */
	comedi_kfree(entry_name);
}

#else /* !CONFIG_PROC_FS */

int comedi_proc_attach(comedi_cxt_t * cxt)
{
	return 0;
}

void comedi_proc_detach(comedi_cxt_t * cxt)
{
}

#endif /* CONFIG_PROC_FS */

/* --- Attach / detach section --- */

int comedi_fill_lnkdesc(comedi_cxt_t * cxt,
			comedi_lnkdesc_t * link_arg, void *arg)
{
	int ret;
	char *tmpname = NULL;
	void *tmpopts = NULL;

	comedi_loginfo("comedi_fill_lnkdesc: minor=%d\n",
		       comedi_get_minor(cxt));

	ret = comedi_copy_from_user(cxt,
				    link_arg, arg, sizeof(comedi_lnkdesc_t));
	if (ret != 0) {
		comedi_logerr
		    ("comedi_fill_lnkdesc: call1(copy_from_user) failed\n");
		goto out_get_lnkdesc;
	}

	if (link_arg->bname_size != 0 && link_arg->bname != NULL) {
		tmpname = comedi_kmalloc(link_arg->bname_size + 1);
		if (tmpname == NULL) {
			comedi_logerr
			    ("comedi_fill_lnkdesc: call1(alloc) failed\n");
			ret = -ENOMEM;
			goto out_get_lnkdesc;
		}
		tmpname[link_arg->bname_size] = 0;

		ret = comedi_copy_from_user(cxt,
					    tmpname,
					    link_arg->bname,
					    link_arg->bname_size);
		if (ret != 0) {
			comedi_logerr
			    ("comedi_fill_lnkdesc: call2(copy_from_user) failed\n");
			goto out_get_lnkdesc;
		}
	} else {
		comedi_logerr("comedi_fill_lnkdesc: board name missing\n");
		ret = -EINVAL;
		goto out_get_lnkdesc;
	}

	if (link_arg->opts_size != 0 && link_arg->opts != NULL) {
		tmpopts = comedi_kmalloc(link_arg->opts_size);

		if (tmpopts == NULL) {
			comedi_logerr
			    ("comedi_fill_lnkdesc: call2(alloc) failed\n");
			ret = -ENOMEM;
			goto out_get_lnkdesc;
		}

		ret = comedi_copy_from_user(cxt,
					    tmpopts,
					    link_arg->opts,
					    link_arg->opts_size);
		if (ret != 0) {
			comedi_logerr
			    ("comedi_fill_lnkdesc: call3(copy_from_user) failed\n");
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

void comedi_free_lnkdesc(comedi_cxt_t * cxt, comedi_lnkdesc_t * link_arg)
{
	comedi_loginfo("comedi_free_lnkdesc: minor=%d\n",
		       comedi_get_minor(cxt));

	if (link_arg->bname != NULL)
		comedi_kfree(link_arg->bname);

	if (link_arg->opts != NULL)
		comedi_kfree(link_arg->opts);
}

int comedi_assign_driver(comedi_cxt_t * cxt,
			 comedi_drv_t * drv, comedi_lnkdesc_t * link_arg)
{
	int ret = 0;
	comedi_dev_t *dev = comedi_get_dev(cxt);

	comedi_loginfo("comedi_assign_driver: minor=%d\n",
		       comedi_get_minor(cxt));

	dev->driver = drv;

	if (drv->privdata_size == 0)
		comedi_loginfo
		    ("comedi_assign_driver: warning! the field priv will not be usable\n");
	else {
		dev->priv = comedi_kmalloc(drv->privdata_size);
		if (dev->priv == NULL && drv->privdata_size != 0) {
			comedi_logerr
			    ("comedi_assign_driver: call(alloc) failed\n");
			ret = -ENOMEM;
			goto out_assign_driver;
		}
	}

	if ((ret = drv->attach(cxt, link_arg)) != 0)
		comedi_logerr
		    ("comedi_assign_driver: call(drv->attach) failed (ret=%d)\n",
		     ret);

      out_assign_driver:

	/* Increments module's count */
	if (ret == 0 && (!try_module_get(drv->owner)))
		ret = -ENODEV;

	if (ret != 0 && dev->priv != NULL) {
		comedi_kfree(dev->priv);
		dev->driver = NULL;
	}

	return ret;
}

int comedi_release_driver(comedi_cxt_t * cxt)
{
	int ret = 0;
	unsigned long flags;
	comedi_dev_t *dev = comedi_get_dev(cxt);

	comedi_loginfo("comedi_release_driver: minor=%d\n",
		       comedi_get_minor(cxt));

	comedi_lock_irqsave(&dev->lock, flags);

	if ((ret = dev->driver->detach(cxt)) != 0)
		goto out_release_driver;

	/* Decreases module's count 
	   so as to allow module unloading */
	module_put(dev->driver->owner);

	comedi_kfree(dev->priv);
	dev->driver = NULL;

      out_release_driver:
	comedi_unlock_irqrestore(&dev->lock, flags);

	return ret;
}

int comedi_device_attach(comedi_cxt_t * cxt, void *arg)
{
	int ret = 0;
	comedi_lnkdesc_t link_arg;
	comedi_drv_t *drv = NULL;

	comedi_loginfo("comedi_device_attach: minor=%d\n",
		       comedi_get_minor(cxt));

	if ((ret = comedi_fill_lnkdesc(cxt, &link_arg, arg)) != 0)
		goto out_attach;

	if ((ret = comedi_lct_drv(link_arg.bname, &drv)) != 0)
		goto out_attach;

	if ((ret = comedi_assign_driver(cxt, drv, &link_arg)) != 0)
		goto out_attach;

      out_attach:
	comedi_free_lnkdesc(cxt, &link_arg);
	return ret;
}

int comedi_device_detach(comedi_cxt_t * cxt)
{
	comedi_dev_t *dev = comedi_get_dev(cxt);

	comedi_loginfo("comedi_device_detach: minor=%d\n",
		       comedi_get_minor(cxt));

	if (dev->driver == NULL)
		return -ENXIO;

	return comedi_release_driver(cxt);
}

/* --- IOCTL / FOPS functions --- */

int comedi_ioctl_devcfg(comedi_cxt_t * cxt, void *arg)
{
	int ret = 0;

	comedi_loginfo("comedi_ioctl_devcfg: minor=%d\n",
		       comedi_get_minor(cxt));

	if (comedi_test_rt() != 0)
		return -EPERM;

	if (arg == NULL) {
		/* Basic checking */
		if (!test_bit
		    (COMEDI_DEV_ATTACHED, &(comedi_get_dev(cxt)->flags)))
			return -EINVAL;
		/* Removes the related proc file */
		comedi_proc_detach(cxt);
		/* Frees the transfer structure and its related data */
		if ((ret = comedi_cleanup_transfer(cxt)) != 0)
			return ret;
		/* Frees the device and the driver from each other */
		if ((ret = comedi_device_detach(cxt)) == 0)
			clear_bit(COMEDI_DEV_ATTACHED,
				  &(comedi_get_dev(cxt)->flags));
	} else {
		/* Basic checking */
		if (test_bit
		    (COMEDI_DEV_ATTACHED, &(comedi_get_dev(cxt)->flags)))
			return -EINVAL;
		/* Links the device with the driver */
		if ((ret = comedi_device_attach(cxt, arg)) != 0)
			return ret;
		/* Creates the transfer structure and
		   the related proc file */
		if ((ret = comedi_setup_transfer(cxt)) != 0 ||
		    (ret = comedi_proc_attach(cxt)) != 0)
			comedi_device_detach(cxt);
		else
			set_bit(COMEDI_DEV_ATTACHED,
				&(comedi_get_dev(cxt)->flags));
	}

	return ret;
}

int comedi_ioctl_devinfo(comedi_cxt_t * cxt, void *arg)
{
	comedi_dvinfo_t info;
	comedi_dev_t *dev = comedi_get_dev(cxt);

	comedi_loginfo("comedi_ioctl_devinfo: minor=%d\n",
		       comedi_get_minor(cxt));

	memset(&info, 0, sizeof(comedi_dvinfo_t));

	if (test_bit(COMEDI_DEV_ATTACHED, &dev->flags)) {
		int len = (strlen(dev->driver->board_name) > COMEDI_NAMELEN) ?
		    COMEDI_NAMELEN : strlen(dev->driver->board_name);

		memcpy(info.board_name, dev->driver->board_name, len);
		info.nb_subd = dev->transfer->nb_subd;
		info.idx_read_subd = dev->transfer->idx_read_subd;
		info.idx_write_subd = dev->transfer->idx_write_subd;
	}

	if (comedi_copy_to_user(cxt, arg, &info, sizeof(comedi_dvinfo_t)) != 0)
		return -EINVAL;

	return 0;
}

#endif /* !DOXYGEN_CPP */

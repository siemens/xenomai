/**
 * @file
 * Comedi for RTDM, driver related features
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

#include <comedi/context.h>
#include <comedi/device.h>
#include <comedi/driver.h>

#include "proc.h"

static LIST_HEAD(comedi_drvs);

/* --- Driver list management functions --- */

int comedi_lct_drv(char *pin, comedi_drv_t ** pio)
{
	struct list_head *this;
	int ret = -EINVAL;

	comedi_loginfo("comedi_lct_drv: name=%s\n", pin);

	/* Goes through the linked list so as to find 
	   a driver instance with the same name */
	list_for_each(this, &comedi_drvs) {
		comedi_drv_t *drv = list_entry(this, comedi_drv_t, list);

		if (strcmp(drv->board_name, pin) == 0) {
			/* The argument pio can be NULL 
			   if there is no need to retrieve the pointer */
			if (pio != NULL)
				*pio = drv;
			ret = 0;
			break;
		}
	}

	return ret;
}

int comedi_add_drv(comedi_drv_t * drv)
{
	comedi_loginfo("comedi_add_drv: name=%s\n", drv->board_name);

	if (comedi_lct_drv(drv->board_name, NULL) != 0) {
		list_add(&drv->list, &comedi_drvs);
		return 0;
	} else
		return -EINVAL;
}

int comedi_rm_drv(comedi_drv_t * drv)
{
	comedi_loginfo("comedi_rm_drv: name=%s\n", drv->board_name);

	if (comedi_lct_drv(drv->board_name, NULL) == 0) {
		/* Here, we consider the argument is pointing
		   to a real driver struct (not a blank structure
		   with only the name field properly set */
		list_del(&drv->list);
		return 0;
	} else
		return -EINVAL;
}

#ifdef CONFIG_PROC_FS

/* --- Driver list proc section --- */

int comedi_rdproc_drvs(char *page,
		       char **start, off_t off, int count, int *eof, void *data)
{
	int i = 0, len = 0;
	char *p = page;
	struct list_head *this;

	p += sprintf(p, "--  Comedi drivers --\n\n");
	p += sprintf(p, "| idx | driver name\n");

	list_for_each(this, &comedi_drvs) {
		comedi_drv_t *drv = list_entry(this, comedi_drv_t, list);

		p += sprintf(p, "|  %02d | %s\n", i++, drv->board_name);
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

#endif /* CONFIG_PROC_FS */

/* --- Driver initialization / cleanup functions --- */

int comedi_init_drv(comedi_drv_t * drv)
{
	if (drv == NULL)
		return -EINVAL;

	memset(drv, 0, sizeof(comedi_drv_t));
	/* The linked list initialization is the only reason 
	   why comedi_init_drv() is mandatory before 
	   registering the driver */
	INIT_LIST_HEAD(&drv->subdvsq);

	return 0;
}

int comedi_cleanup_drv(comedi_drv_t * drv)
{
	while (&drv->subdvsq != drv->subdvsq.next) {
		struct list_head *this = drv->subdvsq.next;
		comedi_subd_t *tmp = list_entry(this, comedi_subd_t, list);

		list_del(this);
		comedi_kfree(tmp);
	}

	return 0;
}

#endif /* !DOXYGEN_CPP */

/*
 * Copyright (C) 2014 Philippe Gerum <rpm@xenomai.org>
 *
 * Xenomai is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#ifndef _COBALT_RTDM_UDD_H
#define _COBALT_RTDM_UDD_H

#include <linux/list.h>
#include <rtdm/driver.h>
#include <rtdm/uapi/udd.h>

#define UDD_IRQ_NONE     0
#define UDD_IRQ_CUSTOM   (-1)

#define UDD_MEM_NONE     0
#define UDD_MEM_PHYS     1
#define UDD_MEM_LOGICAL  2
#define UDD_MEM_VIRTUAL  3

#define UDD_NR_MAPS  5

struct udd_memregion {
	const char *name;
	phys_addr_t addr;
	size_t len;
	int type;
};

struct udd_device {
	const char *device_name;
	const char *device_description;
	int device_subclass;
	int driver_version;
	const char *driver_author;
	struct {
		int (*open)(struct udd_device *dev, int oflags);
		void (*close)(struct udd_device *dev);
		int (*ioctl)(struct udd_device *dev,
			     unsigned int request, void *arg);
		int (*mmap)(struct udd_device *dev,
			    struct vm_area_struct *vma);
		int (*interrupt)(struct udd_device *dev);
	} ops;
	int irq;
	struct udd_memregion mem_regions[UDD_NR_MAPS];
	struct udd_reserved {
		rtdm_irq_t irqh;
		atomic_t event;
		struct udd_signotify signfy;
		struct rtdm_event pulse;
		struct rtdm_device device;
		struct rtdm_device mapper;
		char *mapper_name;
		int nr_maps;
	} __reserved;
};

int udd_register_device(struct udd_device *dev);

int udd_unregister_device(struct udd_device *dev,
			  unsigned int poll_delay);

void udd_notify_event(struct udd_device *udd);

void udd_post_irq_enable(int irq);

void udd_post_irq_disable(int irq);

#endif /* !_COBALT_RTDM_UDD_H */

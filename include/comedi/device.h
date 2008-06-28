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

#ifndef __COMEDI_DEVICE__
#define __COMEDI_DEVICE__

#ifndef DOXYGEN_CPP

#include <comedi/context.h>
#include <comedi/driver.h>
#include <comedi/transfer.h>

#ifdef __KERNEL__

#define COMEDI_NB_DEVICES 10

#define COMEDI_DEV_ATTACHED 0

struct comedi_device {

	/* Spinlock for global device use */
	comedi_lock_t lock;

	/* Device specific flags */
	unsigned long flags;

	/* Driver assigned to this device thanks to attaching
	   procedure */
	comedi_drv_t *driver;

	/* TODO: transfer should not be a pointer */
	comedi_trf_t *transfer;

	/* Private data useful for drivers functioning */
	void *priv;
};
typedef struct comedi_device comedi_dev_t;

#endif /* __KERNEL__ */

/* DEVCFG ioctl argument structure */
struct comedi_link_desc {
	unsigned char bname_size;
	char *bname;
	unsigned int opts_size;
	void *opts;
};
typedef struct comedi_link_desc comedi_lnkdesc_t;

/* DEVINFO ioctl argument structure */
struct comedi_dev_info {
	char board_name[COMEDI_NAMELEN];
	int nb_subd;
	int idx_read_subd;
	int idx_write_subd;
};
typedef struct comedi_dev_info comedi_dvinfo_t;

#ifdef __KERNEL__

/* --- Device related macro --- */
#define comedi_check_dev(x) test_bit(COMEDI_DEV_ATTACHED, &(x->flags))

/* --- Devices tab related functions --- */
void comedi_init_devs(void);
int comedi_check_cleanup_devs(void);
int comedi_rdproc_devs(char *page,
		       char **start,
		       off_t off, int count, int *eof, void *data);

/* --- Context related function / macro --- */
void comedi_set_dev(comedi_cxt_t * cxt);
#define comedi_get_dev(x) ((x)->dev)

/* --- Upper layer functions --- */
int comedi_ioctl_devcfg(comedi_cxt_t * cxt, void *arg);
int comedi_ioctl_devinfo(comedi_cxt_t * cxt, void *arg);

#endif /* __KERNEL__ */

#endif /* !DOXYGEN_CPP */

#endif /* __COMEDI_DEVICE__ */

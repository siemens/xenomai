/**
 * @file
 * Comedi for RTDM, transfer related features
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

#ifndef __COMEDI_TRANSFER__
#define __COMEDI_TRANSFER__

#ifndef DOXYGEN_CPP

#include <comedi/context.h>
#include <comedi/subdevice.h>
#include <comedi/buffer.h>

/* Status flags / bits */
#define COMEDI_TSF_BUSY 0
#define COMEDI_TSF_BULK 1
#define COMEDI_TSF_MMAP 2

/* Fields init values */
#define COMEDI_IRQ_UNUSED (unsigned int)((unsigned short)(~0))
#define COMEDI_IDX_UNUSED (unsigned int)(~0)

/* IRQ types */
#define COMEDI_IRQ_SHARED RTDM_IRQTYPE_SHARED
#define COMEDI_IRQ_EDGE RTDM_IRQTYPE_EDGE
#define COMEDI_IRQ_DISABLED 0

/* Poll timeout values */
#define COMEDI_INFINITE 0
#define COMEDI_NONBLOCK (-1)

#ifdef __KERNEL__

struct comedi_device;

/* Comedi transfer descriptor */
struct comedi_transfer {

	/* Subdevices desc */
	unsigned int nb_subd;
	comedi_subd_t **subds;
	int idx_read_subd;
	int idx_write_subd;

	/* Buffer desc */
	comedi_buf_t **bufs;

	/* IRQ in use */
	comedi_irq_desc_t irq_desc;

	/* Events/status desc */
	unsigned long *status;
};
typedef struct comedi_transfer comedi_trf_t;

/* --- Proc function --- */

int comedi_rdproc_transfer(char *page,
			   char **start,
			   off_t off, int count, int *eof, void *data);

/* --- Upper layer functions --- */

int comedi_setup_transfer(comedi_cxt_t * cxt);
int comedi_cleanup_transfer(comedi_cxt_t * cxt);
int comedi_reserve_transfer(comedi_cxt_t * cxt, int idx_subd);
int comedi_init_transfer(comedi_cxt_t * cxt, comedi_cmd_t * cmd);
int comedi_cancel_transfer(comedi_cxt_t * cxt, int idx_subd);

ssize_t comedi_put(comedi_cxt_t * cxt, void *buf, size_t nbytes);
ssize_t comedi_get(comedi_cxt_t * cxt, void *buf, size_t nbytes);

int comedi_request_irq(struct comedi_device *dev,
		       unsigned int irq,
		       comedi_irq_hdlr_t handler,
		       unsigned long flags, void *cookie);
int comedi_free_irq(struct comedi_device *dev, unsigned int irq);
unsigned int comedi_get_irq(struct comedi_device *dev);

int comedi_ioctl_cancel(comedi_cxt_t * cxt, void *arg);

#endif /* __KERNEL__ */

#endif /* !DOXYGEN_CPP */

#endif /* __COMEDI_TRANSFER__ */

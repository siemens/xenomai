/**
 * @file
 * Comedi for RTDM, driver related features
 *
 * @note Copyright (C) 1997-2000 David A. Schleef <ds@schleef.org>
 * @note Copyright (C) 2008 Alexis Berlemont <alexis.berlemont@free.fr>
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

#ifndef __COMEDI_DRIVER__
#define __COMEDI_DRIVER__

#ifdef __KERNEL__

#include <linux/list.h>

#include <comedi/context.h>

struct comedi_link_desc;

#define COMEDI_DYNAMIC_DRV 0x1

/** Structure containing driver declaration data.
 *
 *  @see rt_task_inquire()
 */
/* Comedi driver descriptor */
struct comedi_driver {

	/* List stuff */
	struct list_head list;
			   /**< List stuff */

	/* Visible description stuff */
	struct module *owner;
			  /**< Pointer to module containing the code */
	unsigned int flags;
			/**< Type / status driver's flags */
	char *board_name;
		       /**< Board name */
	int privdata_size;
		       /**< Size of the driver's private data */

	/* Hidden description stuff */
	struct list_head subdvsq;
			      /**< List containing the subdevices pointers */

	/* Init/destroy procedures */
	int (*attach) (comedi_cxt_t *, struct comedi_link_desc *);
							     /**< Attach procedure */
	int (*detach) (comedi_cxt_t *);
				   /**< Detach procedure */

};
typedef struct comedi_driver comedi_drv_t;

#ifndef DOXYGEN_CPP

/* Driver list related functions */

int comedi_add_drv(comedi_drv_t * drv);
int comedi_rm_drv(comedi_drv_t * drv);
int comedi_lct_drv(char *pin, comedi_drv_t ** pio);
#ifdef CONFIG_PROC_FS
int comedi_rdproc_drvs(char *page,
		       char **start,
		       off_t off, int count, int *eof, void *data);
#endif /* CONFIG_PROC_FS */

/* Driver related functions */

int comedi_init_drv(comedi_drv_t * drv);
int comedi_cleanup_drv(comedi_drv_t * drv);

#endif /* !DOXYGEN_CPP */

#endif /* __KERNEL__ */

#endif /* __COMEDI_DRIVER__ */

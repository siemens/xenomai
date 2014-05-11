/**
 * @file
 * Analogy for Linux, subdevice related features
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
#ifndef _COBALT_RTDM_ANALOGY_SUBDEVICE_H
#define _COBALT_RTDM_ANALOGY_SUBDEVICE_H

#include <linux/list.h>
#include <rtdm/analogy/instruction.h>
#include <rtdm/analogy/command.h>
#include <rtdm/analogy/channel_range.h>

/* --- Subdevice descriptor structure --- */

struct a4l_device;
struct a4l_buffer;

/*!
 * @brief Structure describing the subdevice
 * @see a4l_add_subd()
 */

struct a4l_subdevice {

	struct list_head list;
			   /**< List stuff */

	struct a4l_device *dev;
			       /**< Containing device */

	unsigned int idx;
		      /**< Subdevice index */

	struct a4l_buffer *buf;
			       /**< Linked buffer */

	/* Subdevice's status (busy, linked?) */
	unsigned long status;
			     /**< Subdevice's status */

	/* Descriptors stuff */
	unsigned long flags;
			 /**< Type flags */
	a4l_chdesc_t *chan_desc;
				/**< Tab of channels descriptors pointers */
	a4l_rngdesc_t *rng_desc;
				/**< Tab of ranges descriptors pointers */
	a4l_cmd_t *cmd_mask;
			    /**< Command capabilities mask */

	/* Functions stuff */
	int (*insn_read) (struct a4l_subdevice *, a4l_kinsn_t *);
							/**< Callback for the instruction "read" */
	int (*insn_write) (struct a4l_subdevice *, a4l_kinsn_t *);
							 /**< Callback for the instruction "write" */
	int (*insn_bits) (struct a4l_subdevice *, a4l_kinsn_t *);
							/**< Callback for the instruction "bits" */
	int (*insn_config) (struct a4l_subdevice *, a4l_kinsn_t *);
							  /**< Callback for the configuration instruction */
	int (*do_cmd) (struct a4l_subdevice *, a4l_cmd_t *);
					/**< Callback for command handling */
	int (*do_cmdtest) (struct a4l_subdevice *, a4l_cmd_t *);
						       /**< Callback for command checking */
	void (*cancel) (struct a4l_subdevice *);
					 /**< Callback for asynchronous transfer cancellation */
	void (*munge) (struct a4l_subdevice *, void *, unsigned long);
								/**< Callback for munge operation */
	int (*trigger) (struct a4l_subdevice *, lsampl_t);
					      /**< Callback for trigger operation */

	char priv[0];
		  /**< Private data */
};
typedef struct a4l_subdevice a4l_subd_t;

/*! @} subdevice */

/* --- Subdevice related functions and macros --- */

a4l_chan_t *a4l_get_chfeat(a4l_subd_t * sb, int idx);
a4l_rng_t *a4l_get_rngfeat(a4l_subd_t * sb, int chidx, int rngidx);
int a4l_check_chanlist(a4l_subd_t * subd,
		       unsigned char nb_chan, unsigned int *chans);

#define a4l_subd_is_input(x) ((A4L_SUBD_MASK_READ & (x)->flags) != 0)
/* The following macro considers that a DIO subdevice is firstly an
   output subdevice */
#define a4l_subd_is_output(x) \
	((A4L_SUBD_MASK_WRITE & (x)->flags) != 0 || \
	 (A4L_SUBD_DIO & (x)->flags) != 0)

/* --- Upper layer functions --- */

a4l_subd_t * a4l_get_subd(struct a4l_device *dev, int idx);
a4l_subd_t * a4l_alloc_subd(int sizeof_priv,
			    void (*setup)(a4l_subd_t *));
int a4l_add_subd(struct a4l_device *dev, a4l_subd_t * subd);
int a4l_ioctl_subdinfo(a4l_cxt_t * cxt, void *arg);
int a4l_ioctl_chaninfo(a4l_cxt_t * cxt, void *arg);
int a4l_ioctl_rnginfo(a4l_cxt_t * cxt, void *arg);
int a4l_ioctl_nbchaninfo(a4l_cxt_t * cxt, void *arg);
int a4l_ioctl_nbrnginfo(a4l_cxt_t * cxt, void *arg);

#endif /* !_COBALT_RTDM_ANALOGY_SUBDEVICE_H */

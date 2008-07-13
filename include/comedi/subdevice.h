/**
 * @file
 * Comedi for RTDM, subdevice related features
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

#ifndef __COMEDI_SUBDEVICE__
#define __COMEDI_SUBDEVICE__

#ifdef __KERNEL__
#include <linux/list.h>
#endif /* __KERNEL__ */

#include <comedi/types.h>
#include <comedi/context.h>
#include <comedi/instruction.h>
#include <comedi/command.h>
#include <comedi/channel_range.h>

/* --- Subdevice flags desc stuff --- */

/* TODO: replace COMEDI_SUBD_AI with COMEDI_SUBD_ANALOG
   and COMEDI_SUBD_INPUT */

/* Subdevice types masks */
#define COMEDI_SUBD_MASK_READ 0x80000000
#define COMEDI_SUBD_MASK_WRITE 0x40000000
#define COMEDI_SUBD_MASK_SPECIAL 0x20000000

/*!
 * @addtogroup subdevice
 * @{
 */

/*!
 * @anchor COMEDI_SUBD_xxx @name Subdevices types
 * @brief Flags to define the subdevice type
 * @{
 */

/** 
 * Unused subdevice
 */
#define COMEDI_SUBD_UNUSED (COMEDI_SUBD_MASK_SPECIAL|0x1)
/** 
 * Analog input subdevice
 */
#define COMEDI_SUBD_AI (COMEDI_SUBD_MASK_READ|0x2)
/** 
 * Analog output subdevice
 */
#define COMEDI_SUBD_AO (COMEDI_SUBD_MASK_WRITE|0x4)
/** 
 * Digital input subdevice
 */
#define COMEDI_SUBD_DI (COMEDI_SUBD_MASK_READ|0x8)
/** 
 * Digital output subdevice
 */
#define COMEDI_SUBD_DO (COMEDI_SUBD_MASK_WRITE|0x10)
/** 
 * Digital input/output subdevice
 */
#define COMEDI_SUBD_DIO (COMEDI_SUBD_MASK_SPECIAL|0x20)
/** 
 * Counter subdevice
 */
#define COMEDI_SUBD_COUNTER (COMEDI_SUBD_MASK_SPECIAL|0x40)
/** 
 * Timer subdevice
 */
#define COMEDI_SUBD_TIMER (COMEDI_SUBD_MASK_SPECIAL|0x80)
/** 
 * Memory, EEPROM, DPRAM
 */
#define COMEDI_SUBD_MEMORY (COMEDI_SUBD_MASK_SPECIAL|0x100)
/** 
 * Calibration subdevice  DACs
 */
#define COMEDI_SUBD_CALIB (COMEDI_SUBD_MASK_SPECIAL|0x200)
/** 
 * Processor, DSP
 */
#define COMEDI_SUBD_PROC (COMEDI_SUBD_MASK_SPECIAL|0x400)
/** 
 * Serial IO subdevice
 */
#define COMEDI_SUBD_SERIAL (COMEDI_SUBD_MASK_SPECIAL|0x800)
/** 
 * Mask which gathers all the types
 */
#define COMEDI_SUBD_TYPES (COMEDI_SUBD_UNUSED | \
			   COMEDI_SUBD_AI | \
			   COMEDI_SUBD_AO | \
			   COMEDI_SUBD_DI | \
			   COMEDI_SUBD_DO | \
			   COMEDI_SUBD_DIO | \
			   COMEDI_SUBD_COUNTER | \
			   COMEDI_SUBD_CALIB | \
			   COMEDI_SUBD_PROC | \
			   COMEDI_SUBD_SERIAL)

	  /*! @} COMEDI_SUBD_xxx */

/*!
 * @anchor COMEDI_SUBD_FT_xxx @name Subdevice features
 * @brief Flags to define the subdevice's capabilities
 * @{
 */

/* Subdevice capabilities */
/** 
 * The subdevice can handle command (i.e it can perform asynchronous
 * acquisition)
 */
#define COMEDI_SUBD_CMD 0x1000
/** 
 * The subdevice support mmap operations (technically, any driver can
 * do it; however, the developer might want that his driver must be
 * accessed through read / write
 */
#define COMEDI_SUBD_MMAP 0x8000

	  /*! @} COMEDI_SUBD_FT_xxx */

#ifdef __KERNEL__

/* --- Subdevice descriptor structure --- */

struct comedi_device;
struct comedi_driver;

/*! 
 * @brief Structure describing the subdevice
 * @see comedi_add_subd()
 */

struct comedi_subdevice {

	struct list_head list;
			   /**< List stuff */

	/* Descriptors stuff */
	unsigned long flags;
			 /**< Type flags */
	comedi_chdesc_t *chan_desc;
				/**< Tab of channels descriptors pointers */
	comedi_rngdesc_t *rng_desc;
				/**< Tab of ranges descriptors pointers */
	comedi_cmd_t *cmd_mask;
			    /**< Command capabilities mask */

	/* Functions stuff */
	int (*insn_read) (comedi_cxt_t *, comedi_kinsn_t *);
							/**< Callback for the instruction "read" */
	int (*insn_write) (comedi_cxt_t *, comedi_kinsn_t *);
							 /**< Callback for the instruction "write" */
	int (*insn_bits) (comedi_cxt_t *, comedi_kinsn_t *);
							/**< Callback for the instruction "bits" */
	int (*insn_config) (comedi_cxt_t *, comedi_kinsn_t *);
							  /**< Callback for the configuration instruction */
	int (*do_cmd) (comedi_cxt_t *, int);
					/**< Callback for command handling */
	int (*do_cmdtest) (comedi_cxt_t *, comedi_cmd_t *);
						       /**< Callback for command checking */
	int (*cancel) (comedi_cxt_t *, int);
					 /**< Callback for asynchronous transfer cancellation */
	void (*munge) (comedi_cxt_t *, int, void *, unsigned long);
								/**< Callback for munge operation */
	int (*trigger) (comedi_cxt_t *, lsampl_t);
					      /**< Callback for trigger operation */

};
typedef struct comedi_subdevice comedi_subd_t;

#endif /* __KERNEL__ */

	  /*! @} subdevice */

#ifndef DOXYGEN_CPP

/* --- Subdevice related IOCTL arguments structures --- */

/* SUDBINFO IOCTL argument */
struct comedi_subd_info {
	unsigned long flags;
	unsigned long status;
	unsigned char nb_chan;
};
typedef struct comedi_subd_info comedi_sbinfo_t;

/* CHANINFO / NBCHANINFO IOCTL arguments */
struct comedi_chan_info {
	unsigned long chan_flags;
	unsigned char nb_rng;
	unsigned char nb_bits;
};
typedef struct comedi_chan_info comedi_chinfo_t;

struct comedi_chinfo_arg {
	unsigned int idx_subd;
	void *info;
};
typedef struct comedi_chinfo_arg comedi_chinfo_arg_t;

/* RNGINFO / NBRNGINFO IOCTL arguments */
struct comedi_rng_info {
	long min;
	long max;
	unsigned long flags;
};
typedef struct comedi_rng_info comedi_rnginfo_t;

struct comedi_rng_info_arg {
	unsigned int idx_subd;
	unsigned int idx_chan;
	void *info;
};
typedef struct comedi_rng_info_arg comedi_rnginfo_arg_t;

#ifdef __KERNEL__

/* --- Subdevice related functions --- */
comedi_chan_t *comedi_get_chfeat(comedi_subd_t * sb, int idx);
comedi_rng_t *comedi_get_rngfeat(comedi_subd_t * sb,
				      int chidx, int rngidx);
int comedi_check_chanlist(comedi_subd_t * subd,
			  unsigned char nb_chan, unsigned int *chans);

/* --- Upper layer functions --- */
int comedi_add_subd(struct comedi_driver *drv, comedi_subd_t * subd);
int comedi_get_nbchan(struct comedi_device *dev, int subd_key);
int comedi_ioctl_subdinfo(comedi_cxt_t * cxt, void *arg);
int comedi_ioctl_chaninfo(comedi_cxt_t * cxt, void *arg);
int comedi_ioctl_rnginfo(comedi_cxt_t * cxt, void *arg);
int comedi_ioctl_nbchaninfo(comedi_cxt_t * cxt, void *arg);
int comedi_ioctl_nbrnginfo(comedi_cxt_t * cxt, void *arg);

#endif /* __KERNEL__ */

#endif /* !DOXYGEN_CPP */

#endif /* __COMEDI_SUBDEVICE__ */

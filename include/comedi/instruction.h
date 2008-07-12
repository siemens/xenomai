/**
 * @file
 * Comedi for RTDM, instruction related features
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

#ifndef __COMEDI_INSTRUCTION__
#define __COMEDI_INSTRUCTION__

#include <comedi/types.h>
#include <comedi/context.h>

#define COMEDI_INSN_MASK_READ 0x8000000
#define COMEDI_INSN_MASK_WRITE 0x4000000
#define COMEDI_INSN_MASK_SPECIAL 0x2000000

/*!
 * @addtogroup sync1_lib
 * @{
 */

/*!
 * @anchor COMEDI_INSN_xxx @name Instruction type
 * @brief Flags to define the type of instruction
 * @{
 */

/** 
 * Read instruction
 */
#define COMEDI_INSN_READ (0 | COMEDI_INSN_MASK_READ)
/** 
 * Write instruction
 */
#define COMEDI_INSN_WRITE (1 | COMEDI_INSN_MASK_WRITE)
/** 
 * "Bits" instruction
 */
#define COMEDI_INSN_BITS (2 | COMEDI_INSN_MASK_READ | \
			  COMEDI_INSN_MASK_WRITE)
/** 
 * Configuration instruction
 */
#define COMEDI_INSN_CONFIG (3 | COMEDI_INSN_MASK_READ | \
			    COMEDI_INSN_MASK_WRITE)
/** 
 * Get time instruction
 */
#define COMEDI_INSN_GTOD (4 | COMEDI_INSN_MASK_READ | \
			  COMEDI_INSN_MASK_SPECIAL)
/** 
 * Wait instruction
 */
#define COMEDI_INSN_WAIT (5 | COMEDI_INSN_MASK_WRITE | \
			  COMEDI_INSN_MASK_SPECIAL)
/** 
 * Trigger instruction (to start asynchronous acquisition)
 */
#define COMEDI_INSN_INTTRIG (6 | COMEDI_INSN_MASK_WRITE | \
			     COMEDI_INSN_MASK_SPECIAL)

	  /*! @} COMEDI_INSN_xxx */

/** 
 * Maximal wait duration
 */
#define COMEDI_INSN_WAIT_MAX 100000

/*! 
 * @brief Structure describing the synchronous instruction
 * @see comedi_snd_insn()
 */

struct comedi_instruction {
	unsigned int type;
		       /**< Instruction type */
	unsigned int idx_subd;
			   /**< Subdevice to which the instruction will be applied. */
	unsigned int chan_desc;
			    /**< Channel descriptor */
	unsigned int data_size;
			    /**< Size of the intruction data */
	lsampl_t *data;
		    /**< Instruction data */
};
typedef struct comedi_instruction comedi_insn_t;

/*! 
 * @brief Structure describing the list of synchronous instructions
 * @see comedi_snd_insnlist()
 */

struct comedi_instruction_list {
	unsigned int count;
			/**< Instructions count */
	comedi_insn_t *insns;
			  /**< Tab containing the instructions pointers */
};
typedef struct comedi_instruction_list comedi_insnlst_t;

	  /*! @} sync1_lib */

#if defined(__KERNEL__) && !defined(DOXYGEN_CPP)

struct comedi_kernel_instruction {
	unsigned int type;
	unsigned int idx_subd;
	unsigned int chan_desc;
	unsigned int data_size;
	lsampl_t *data;
	lsampl_t *__udata;
};
typedef struct comedi_kernel_instruction comedi_kinsn_t;

struct comedi_kernel_instruction_list {
	unsigned int count;
	comedi_kinsn_t *insns;
	comedi_kinsn_t *__uinsns;
};
typedef struct comedi_kernel_instruction_list comedi_kilst_t;

/* Instruction related functions */

/* Upper layer functions */
int comedi_ioctl_insnlist(comedi_cxt_t * cxt, void *arg);
int comedi_ioctl_insn(comedi_cxt_t * cxt, void *arg);

#endif /* __KERNEL__ && !DOXYGEN_CPP */

#endif /* __COMEDI_INSTRUCTION__ */

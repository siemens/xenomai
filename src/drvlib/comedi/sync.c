/**
 * @file
 * Comedilib for RTDM, instruction related features  
 * @note Copyright (C) 1997-2000 David A. Schleef <ds@schleef.org>
 * @note Copyright (C) 2008 Alexis Berlemont <alexis.berlemont@free.fr>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#include <errno.h>

#include <comedi/ioctl.h>
#include <comedi/comedi.h>

#include "syscall.h"

/*!
 * @ingroup Comedilib4RTDM
 * @defgroup level1_lib Level 1 API
 * @{
 */

/*!
 * @ingroup level1_lib
 * @defgroup sync1_lib Synchronous acquisition API
 * @{
 */

/**
 * @brief Perform a list of synchronous acquisition misc operations
 *
 * The function comedi_snd_insnlist() is able to send many synchronous
 * instructions on a various set of subdevices, channels, etc.
 *
 * @param[in] dsc Device descriptor filled by comedi_open() (and
 * optionally comedi_fill_desc())
 * @param[in] arg Instructions list structure
 *
 * @return 0 on success, otherwise a negative error code.
 *
 */
int comedi_snd_insnlist(comedi_desc_t * dsc, comedi_insnlst_t * arg)
{
	/* Basic checking */
	if (dsc == NULL || dsc->fd < 0)
		return -EINVAL;

	return __sys_ioctl(dsc->fd, COMEDI_INSNLIST, arg);
}

/**
 * @brief Perform a synchronous acquisition misc operation
 *
 * The function comedi_snd_insn() triggers a synchronous acquisition.
 *
 * @param[in] dsc Device descriptor filled by comedi_open() (and
 * optionally comedi_fill_desc())
 * @param[in] arg Instruction structure
 *
 * @return 0 on success, otherwise a negative error code.
 *
 */
int comedi_snd_insn(comedi_desc_t * dsc, comedi_insn_t * arg)
{
	/* Basic checking */
	if (dsc == NULL || dsc->fd < 0)
		return -EINVAL;

	return __sys_ioctl(dsc->fd, COMEDI_INSN, arg);
}

/** @} Synchronous acquisition API */

/** @} Level 1 API */

/*!
 * @ingroup Comedilib4RTDM
 * @defgroup level2_lib Level 2 API
 * @{
 */

/*!
 * @ingroup level2_lib
 * @defgroup sync2_lib Synchronous acquisition API
 * @{
 */

/**
 * @brief Perform a synchronous acquisition write operation
 *
 * @param[in] dsc Device descriptor filled by comedi_open() (and
 * optionally comedi_fill_desc())
 * @param[in] idx_subd Index of the concerned subdevice
 * @param[in] chan_desc Channel descriptor (channel, range and
 * reference)
 * @param[in] ns_delay Optional delay (in nanoseconds) to wait between
 * the setting of the input channel and sample(s) acquisition(s).
 * @param[in] buf Output buffer
 * @param[in] nbyte Number of bytes to write
 *
 * @return 0 on success, otherwise a negative error code.
 *
 */
int comedi_sync_write(comedi_desc_t * dsc,
		      unsigned int idx_subd,
		      unsigned int chan_desc,
		      unsigned int ns_delay, void *buf, size_t nbyte)
{
	int ret;
	comedi_insn_t insn_tab[2] = {
		{
		      type:COMEDI_INSN_WRITE,
		      idx_subd:idx_subd,
		      chan_desc:chan_desc,
		      data_size:0,
	      data:buf},
		{
		      type:COMEDI_INSN_WAIT,
		      idx_subd:idx_subd,
		      chan_desc:chan_desc,
		      data_size:1,
	      data:NULL}
	};

	/* If some delay needs to be applied,
	   the instruction list feature is needed */
	if (ns_delay != 0) {
		int ret;
		lsampl_t _delay = (lsampl_t) ns_delay;
		comedi_insnlst_t insnlst = {
		      count:2,
		      insns:insn_tab
		};

		/* Sets the delay to wait */
		insn_tab[1].data = &_delay;

		/* Sends the two instructions (false read + wait) 
		   to the Comedi layer */
		ret = comedi_snd_insnlist(dsc, &insnlst);
		if (ret < 0)
			return ret;
	}

	/* The first instruction structure must be updated so as 
	   to write the proper data amount */
	insn_tab[0].data_size = nbyte;

	/* Sends the write instruction to the Comedi layer */
	ret = comedi_snd_insn(dsc, insn_tab);

	return (ret == 0) ? nbyte : ret;
}

/**
 * @brief Perform a synchronous acquisition read operation
 *
 * @param[in] dsc Device descriptor filled by comedi_open() (and
 * optionally comedi_fill_desc())
 * @param[in] idx_subd Index of the concerned subdevice
 * @param[in] chan_desc Channel descriptor (channel, range and
 * reference)
 * @param[in] ns_delay Optional delay (in nanoseconds) to wait between
 * the setting of the input channel and sample(s) acquisition(s).
 * @param[in] buf Input buffer
 * @param[in] nbyte Number of bytes to read
 *
 * @return 0 on success, otherwise a negative error code.
 *
 */
int comedi_sync_read(comedi_desc_t * dsc,
		     unsigned int idx_subd,
		     unsigned int chan_desc,
		     unsigned int ns_delay, void *buf, size_t nbyte)
{
	int ret;
	comedi_insn_t insn_tab[2] = {
		{
		      type:COMEDI_INSN_READ,
		      idx_subd:idx_subd,
		      chan_desc:chan_desc,
		      data_size:0,
	      data:buf},
		{
		      type:COMEDI_INSN_WAIT,
		      idx_subd:idx_subd,
		      chan_desc:chan_desc,
		      data_size:1,
	      data:NULL}
	};

	/* If some delay needs to be applied,
	   the instruction list feature is needed */
	if (ns_delay != 0) {
		int ret;
		lsampl_t _delay = (lsampl_t) ns_delay;
		comedi_insnlst_t insnlst = {
		      count:2,
		      insns:insn_tab
		};

		/* Sets the delay to wait */
		insn_tab[1].data = &_delay;

		/* Sends the two instructions (false read + wait) 
		   to the Comedi layer */
		ret = comedi_snd_insnlist(dsc, &insnlst);
		if (ret < 0)
			return ret;
	}

	/* The first instruction structure must be updated so as 
	   to retrieve the proper data amount */
	insn_tab[0].data_size = nbyte;

	/* Sends the read instruction to the Comedi layer */
	ret = comedi_snd_insn(dsc, insn_tab);

	return (ret == 0) ? nbyte : ret;
}

/** @} Synchronous acquisition API */

/** @} Level 2 API */

/**
 * @file
 * Analogy for Linux, instruction related features
 *
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

#include <stdarg.h>
#include <errno.h>

#include <analogy/ioctl.h>
#include <analogy/analogy.h>

#include "syscall.h"

/*!
 * @ingroup Analogylib4Linux
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
 * The function a4l_snd_insnlist() is able to send many synchronous
 * instructions on a various set of subdevices, channels, etc.
 *
 * @param[in] dsc Device descriptor filled by a4l_open() (and
 * optionally a4l_fill_desc())
 * @param[in] arg Instructions list structure
 *
 * @return 0 on success. Otherwise:
 *
 * - -EINVAL is returned if some argument is missing or wrong (Please,
 *    type "dmesg" for more info)
 * - -EFAULT is returned if a user <-> kernel transfer went wrong
 * - -ENOMEM is returned if the system is out of memory
 *
 */
int a4l_snd_insnlist(a4l_desc_t * dsc, a4l_insnlst_t * arg)
{
	/* Basic checking */
	if (dsc == NULL || dsc->fd < 0)
		return -EINVAL;

	return __sys_ioctl(dsc->fd, A4L_INSNLIST, arg);
}

/**
 * @brief Perform a synchronous acquisition misc operation
 *
 * The function a4l_snd_insn() triggers a synchronous acquisition.
 *
 * @param[in] dsc Device descriptor filled by a4l_open() (and
 * optionally a4l_fill_desc())
 * @param[in] arg Instruction structure
 *
 * @return 0 on success. Otherwise:
 *
 * - -EINVAL is returned if some argument is missing or wrong (Please,
 *    type "dmesg" for more info)
 * - -EFAULT is returned if a user <-> kernel transfer went wrong
 * - -ENOMEM is returned if the system is out of memory
 *
 */
int a4l_snd_insn(a4l_desc_t * dsc, a4l_insn_t * arg)
{
	/* Basic checking */
	if (dsc == NULL || dsc->fd < 0)
		return -EINVAL;

	return __sys_ioctl(dsc->fd, A4L_INSN, arg);
}

/** @} Synchronous acquisition API */

/** @} Level 1 API */

/*!
 * @ingroup Analogylib4Linux
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
 * @param[in] dsc Device descriptor filled by a4l_open() (and
 * optionally a4l_fill_desc())
 * @param[in] idx_subd Index of the concerned subdevice
 * @param[in] chan_desc Channel descriptor (channel, range and
 * reference)
 * @param[in] ns_delay Optional delay (in nanoseconds) to wait between
 * the setting of the input channel and sample(s) acquisition(s).
 * @param[in] buf Output buffer
 * @param[in] nbyte Number of bytes to write
 *
 * @return Number of bytes written, otherwise negative error code:
 *
 * - -EINVAL is returned if some argument is missing or wrong (Please,
 *    type "dmesg" for more info)
 * - -EFAULT is returned if a user <-> kernel transfer went wrong
 * - -ENOMEM is returned if the system is out of memory
 *
 */
int a4l_sync_write(a4l_desc_t * dsc,
		   unsigned int idx_subd,
		   unsigned int chan_desc,
		   unsigned int ns_delay, void *buf, size_t nbyte)
{
	int ret;
	a4l_insn_t insn_tab[2] = {
		{
			.type = A4L_INSN_WRITE,
			.idx_subd = idx_subd,
			.chan_desc = chan_desc,
			.data_size = 0,
			.data = buf
		}, {
			.type = A4L_INSN_WAIT,
			.idx_subd = idx_subd,
			.chan_desc = chan_desc,
			.data_size = 1,
			.data = NULL
		}
	};

	/* If some delay needs to be applied,
	   the instruction list feature is needed */
	if (ns_delay != 0) {
		int ret;
		lsampl_t _delay = (lsampl_t) ns_delay;
		a4l_insnlst_t insnlst = {
			.count = 2,
			.insns = insn_tab
		};

		/* Sets the delay to wait */
		insn_tab[1].data = &_delay;

		/* Sends the two instructions (false read + wait)
		   to the Analogy layer */
		ret = a4l_snd_insnlist(dsc, &insnlst);
		if (ret < 0)
			return ret;
	}

	/* The first instruction structure must be updated so as
	   to write the proper data amount */
	insn_tab[0].data_size = nbyte;

	/* Sends the write instruction to the Analogy layer */
	ret = a4l_snd_insn(dsc, insn_tab);

	return (ret == 0) ? nbyte : ret;
}

/**
 * @brief Perform a synchronous acquisition read operation
 *
 * @param[in] dsc Device descriptor filled by a4l_open() (and
 * optionally a4l_fill_desc())
 * @param[in] idx_subd Index of the concerned subdevice
 * @param[in] chan_desc Channel descriptor (channel, range and
 * reference)
 * @param[in] ns_delay Optional delay (in nanoseconds) to wait between
 * the setting of the input channel and sample(s) acquisition(s).
 * @param[in] buf Input buffer
 * @param[in] nbyte Number of bytes to read
 *
 * @return Number of bytes read, otherwise negative error code:
 *
 * - -EINVAL is returned if some argument is missing or wrong (Please,
 *    type "dmesg" for more info)
 * - -EFAULT is returned if a user <-> kernel transfer went wrong
 * - -ENOMEM is returned if the system is out of memory
 *
 */
int a4l_sync_read(a4l_desc_t * dsc,
		  unsigned int idx_subd,
		  unsigned int chan_desc,
		  unsigned int ns_delay, void *buf, size_t nbyte)
{
	int ret;
	a4l_insn_t insn_tab[2] = {
		{
			.type = A4L_INSN_READ,
			.idx_subd = idx_subd,
			.chan_desc = chan_desc,
			.data_size = 0,
			.data = buf},
		{
			.type = A4L_INSN_WAIT,
			.idx_subd = idx_subd,
			.chan_desc = chan_desc,
			.data_size = 1,
			.data = NULL}
	};

	/* If some delay needs to be applied,
	   the instruction list feature is needed */
	if (ns_delay != 0) {
		int ret;
		lsampl_t _delay = (lsampl_t) ns_delay;
		a4l_insnlst_t insnlst = {
			.count = 2,
			.insns = insn_tab
		};

		/* Sets the delay to wait */
		insn_tab[1].data = &_delay;

		/* Sends the two instructions (false read + wait)
		   to the Analogy layer */
		ret = a4l_snd_insnlist(dsc, &insnlst);
		if (ret < 0)
			return ret;
	}

	/* The first instruction structure must be updated so as
	   to retrieve the proper data amount */
	insn_tab[0].data_size = nbyte;

	/* Sends the read instruction to the Analogy layer */
	ret = a4l_snd_insn(dsc, insn_tab);

	return (ret == 0) ? nbyte : ret;
}

/**
 * @brief Perform a synchronous acquisition digital acquisition
 *
 * @param[in] dsc Device descriptor filled by a4l_open() (and
 * optionally a4l_fill_desc())
 * @param[in] idx_subd Index of the concerned subdevice
 * @param[in] mask Write mask which indicates which bit(s) must be
 * modified
 * @param[in,out] buf Input / output buffer
 *
 * @return Number of bytes read, otherwise negative error code:
 *
 * - -EINVAL is returned if some argument is missing or wrong (Please,
 *    type "dmesg" for more info)
 * - -EFAULT is returned if a user <-> kernel transfer went wrong
 * - -ENOMEM is returned if the system is out of memory
 * - -ENOSYS is returned if the driver does not provide any handler
 *    "instruction bits"
 *
 */
int a4l_sync_dio(a4l_desc_t *dsc,
		 unsigned int idx_subd, void *mask, void *buf)
{
	unsigned char values[16];
	a4l_insn_t insn = {
		.type = A4L_INSN_BITS,
		.idx_subd = idx_subd,
		.data = values,
	};

	int ret, size;
	a4l_sbinfo_t *subd;

	/* Get the subdevice descriptor */
	ret = a4l_get_subdinfo(dsc, idx_subd, &subd);
	if (ret < 0)
		return ret;

	/* Get the size in memory of a DIO acquisition */
	size = a4l_sizeof_subd(subd);

	switch(size) {
	case sizeof(uint32_t): {
		uint32_t *tmp = (uint32_t *)values;
		tmp[0] = *((uint32_t *)mask);
		tmp[1] = *((uint32_t *)buf);
		insn.data_size = 2 * sizeof(uint32_t);
		break;
	}
	case sizeof(uint16_t): {
		uint16_t *tmp = (uint16_t *)values;
		tmp[0] = *((uint16_t *)mask);
		tmp[1] = *((uint16_t *)buf);
		insn.data_size = 2 * sizeof(uint16_t);
		break;
	}
	case sizeof(uint8_t): {
		uint8_t *tmp = (uint8_t *)values;
		tmp[0] = *((uint8_t *)mask);
		tmp[1] = *((uint8_t *)buf);
		insn.data_size = 2 * sizeof(uint8_t);
		break;
	}
	default:
		return -EINVAL;
	}

	/* Send the insn_bits instruction */
	ret = a4l_snd_insn(dsc, &insn);

	/* Update the buffer if need be */
	switch(size) {
	case sizeof(uint32_t): {
		uint32_t *tmp = (uint32_t *)buf;
		*tmp = ((uint32_t *)values)[1];
		break;
	}
	case sizeof(uint16_t): {
		uint16_t *tmp = (uint16_t *)buf;
		*tmp = ((uint16_t *)values)[1];
		break;
	}
	case sizeof(uint8_t): {
		uint8_t *tmp = (uint8_t *)buf;
		*tmp = ((uint8_t *)values)[1];
		break;
	}
	}

	return ret;
}

/**
 * @brief Configure a subdevice
 *
 * a4l_config_subd() takes a variable count of arguments. According to
 * the configuration type, some additional argument is necessary:
 * - A4L_INSN_CONFIG_DIO_INPUT: the channel index (unsigned int)
 * - A4L_INSN_CONFIG_DIO_OUTPUT: the channel index (unsigned int)
 * - A4L_INSN_CONFIG_DIO_QUERY: the returned DIO polarity (unsigned
 *   int *)
 *
 * @param[in] dsc Device descriptor filled by a4l_open() (and
 * optionally a4l_fill_desc())
 * @param[in] idx_subd Index of the concerned subdevice
 * @param[in] type Configuration parameter
 *
 * @return 0 on success. Otherwise:
 *
 * - -EINVAL is returned if some argument is missing or wrong (Please,
 *    type "dmesg" for more info)
 * - -ENOSYS is returned if the configuration parameter is not
 *    supported
 *
 */
int a4l_config_subd(a4l_desc_t * dsc,
		    unsigned int idx_subd, unsigned int type, ...)
{
	unsigned int values[4] = {type, 0, 0, 0};
	a4l_insn_t insn = {
		.type = A4L_INSN_CONFIG,
		.idx_subd = idx_subd,
		.data = values,
	};
	int ret = 0;
	va_list args;

	va_start(args, type);

	/* So far, few config types are supported */
	switch (type) {
	case A4L_INSN_CONFIG_DIO_OUTPUT:
	case A4L_INSN_CONFIG_DIO_INPUT:
	case A4L_INSN_CONFIG_DIO_OPENDRAIN:
	{
		unsigned int idx_chan = va_arg(args, unsigned int);
		insn.chan_desc = CHAN(idx_chan);
		insn.data_size = sizeof(unsigned int);
		break;
	}
	case A4L_INSN_CONFIG_DIO_QUERY:
		insn.data_size = 2 * sizeof(unsigned int);
		break;
	default:
		return -ENOSYS;
	}

	/* Send the config instruction */
	ret = a4l_snd_insn(dsc, &insn);
	if (ret < 0)
		goto out;

	/* Retrieve the result(s), if need be */
	switch (type) {
	case A4L_INSN_CONFIG_DIO_QUERY:
	{
		unsigned int *value = va_arg(args, unsigned int *);
		*value = values[1];
		break;
	}
	default:
		break;
	}

out:
	va_end(args);

	return ret;
}

/** @} Synchronous acquisition API */

/** @} Level 2 API */

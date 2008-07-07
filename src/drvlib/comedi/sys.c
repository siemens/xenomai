/**
 * @file
 * Comedilib for RTDM, descriptor related features  
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

/*!
 * @ingroup Comedi4RTDM
 * @defgroup Comedilib4RTDM Library API.
 *
 * This is the API interface of Comedi4RTDM library
 *
 */

/*!
 * @ingroup Comedilib4RTDM
 * @defgroup syscall Level 0 API (Syscall API)
 *
 * This is the API interface which encapsulates common syscalls
 * structure
 * Warning: this API level should not be used
 */

#include <comedi/ioctl.h>
#include <comedi/comedi.h>

#include "syscall.h"

/*!
 * @ingroup syscall
 * @defgroup basic_sys Basic Syscall API
 * @{
 */

/**
 * @brief Open a Comedi device
 *
 * @param[in] fname Device name
 *
 * @return Positive file descriptor value on success, otherwise a negative
 * error code.
 *
 */
int comedi_sys_open(const char *fname)
{
	return __sys_open(fname);
}

/**
 * @brief Close a Comedi device
 *
 * @param[in] fd File descriptor as returned by comedi_sys_open()
 *
 * @return 0 on success, otherwise a negative error code.
 *
 */
int comedi_sys_close(int fd)
{
	return __sys_close(fd);
}

/**
 * @brief Read from a Comedi device
 *
 * The function comedi_read() is only useful for acquisition
 * configured through a Comedi command.
 *
 * @param[in] fd File descriptor as returned by comedi_sys_open()
 * @param[out] buf Input buffer
 * @param[in] nbyte Number of bytes to read
 *
 * @return Number of bytes read, otherwise negative error code.
 *
 */
int comedi_sys_read(int fd, void *buf, size_t nbyte)
{
	return __sys_read(fd, buf, nbyte);
}

/**
 * @brief Write to a Comedi device
 *
 * The function comedi_write() is only useful for acquisition
 * configured through a Comedi command.
 *
 * @param[in] fd File descriptor as returned by comedi_sys_open()
 * @param[in] buf Output buffer
 * @param[in] nbyte Number of bytes to write
 *
 * @return Number of bytes written, otherwise negative error code.
 *
 */
int comedi_sys_write(int fd, void *buf, size_t nbyte)
{
	return __sys_write(fd, buf, nbyte);
}

/** @} */

/*!
 * @ingroup syscall
 * @defgroup attach_sys Attach / detach Syscall API
 * @{
 */

/**
 * @brief Attach a Comedi device to a driver
 *
 * @param[in] fd File descriptor as returned by comedi_sys_open()
 * @param[in] arg Link descriptor argument
 *
 * @return 0 on success, otherwise a negative error code.
 *
 */
int comedi_sys_attach(int fd, comedi_lnkdesc_t * arg)
{
	return __sys_ioctl(fd, COMEDI_DEVCFG, arg);
}

/**
 * @brief Detach a Comedi device from a driver
 *
 * @param[in] fd File descriptor as returned by comedi_sys_open()
 *
 * @return 0 on success, otherwise a negative error code.
 *
 */
int comedi_sys_detach(int fd)
{
	return __sys_ioctl(fd, COMEDI_DEVCFG, NULL);
}

/** @} */

/**
 * @file
 * Analogy for Linux, descriptor related features
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

/*!
 * @ingroup Analogy4Linux
 * @defgroup Analogylib4Linux Library API.
 *
 * This is the API interface of Analogy library
 *
 */

/*!
 * @ingroup Analogylib4Linux
 * @defgroup syscall Level 0 API (Syscall API)
 *
 * This is the API interface which encapsulates common syscalls
 * structure
 * Warning: this API level should not be used
 */

#include <analogy/ioctl.h>
#include <analogy/analogy.h>

#include "syscall.h"

/*!
 * @ingroup syscall
 * @defgroup basic_sys Basic Syscall API
 * @{
 */

/**
 * @brief Open an Analogy device
 *
 * @param[in] fname Device name
 *
 * @return Positive file descriptor value on success, otherwise a negative
 * error code.
 *
 */
int a4l_sys_open(const char *fname)
{
	return __sys_open(fname);
}

/**
 * @brief Close an Analogy device
 *
 * @param[in] fd File descriptor as returned by a4l_sys_open()
 *
 * @return 0 on success, otherwise a negative error code.
 *
 */
int a4l_sys_close(int fd)
{
	return __sys_close(fd);
}

/**
 * @brief Read from an Analogy device
 *
 * The function a4l_read() is only useful for acquisition
 * configured through an Analogy command.
 *
 * @param[in] fd File descriptor as returned by a4l_sys_open()
 * @param[out] buf Input buffer
 * @param[in] nbyte Number of bytes to read
 *
 * @return Number of bytes read. Otherwise:
 *
 * - -EINVAL is returned if some argument is missing or wrong (Please,
 *    type "dmesg" for more info)
 * - -ENOENT is returned if the device's reading subdevice is idle (no
 *    command was sent)
 * - -EFAULT is returned if a user <-> kernel transfer went wrong
 * - -EINTR is returned if calling task has been unblocked by a signal
 *
 */
int a4l_sys_read(int fd, void *buf, size_t nbyte)
{
	return __sys_read(fd, buf, nbyte);
}

/**
 * @brief Write to an Analogy device
 *
 * The function a4l_write() is only useful for acquisition
 * configured through an Analogy command.
 *
 * @param[in] fd File descriptor as returned by a4l_sys_open()
 * @param[in] buf Output buffer
 * @param[in] nbyte Number of bytes to write
 *
 * @return Number of bytes written. Otherwise:
 *
 * - -EINVAL is returned if some argument is missing or wrong (Please,
 *    type "dmesg" for more info)
 * - -ENOENT is returned if the device's writing subdevice is idle (no
 *    command was sent)
 * - -EFAULT is returned if a user <-> kernel transfer went wrong
 * - -EINTR is returned if calling task has been unblocked by a signal
 *
 */
int a4l_sys_write(int fd, void *buf, size_t nbyte)
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
 * @brief Attach an Analogy device to a driver
 *
 * @param[in] fd File descriptor as returned by a4l_sys_open()
 * @param[in] arg Link descriptor argument
 *
 * @return 0 on success. Otherwise:
 *
 * - -ENOMEM is returned if the system is out of memory
 * - -EINVAL is returned if some argument is missing or wrong (Please,
 *    type "dmesg" for more info)
 * - -EFAULT is returned if a user <-> kernel transfer went wrong
 * - -ENODEV is returned in case of internal error (Please, type
 *    "dmesg" for more info)
 * - -ENXIO is returned in case of internal error (Please, type
 *    "dmesg" for more info)
 *
 */
int a4l_sys_attach(int fd, a4l_lnkdesc_t * arg)
{
	return __sys_ioctl(fd, A4L_DEVCFG, arg);
}

/**
 * @brief Detach an Analogy device from a driver
 *
 * @param[in] fd File descriptor as returned by a4l_sys_open()
 *
 * @return 0 on success. Otherwise:
 *
 * - -EINVAL is returned if some argument is missing or wrong (Please,
 *    type "dmesg" for more info)
 * - -EBUSY is returned if the device to be detached is in use
 * - -EPERM is returned if the devive to be detached still has some
 *    buffer mapped in user-space
 * - -ENODEV is returned in case of internal error (Please, type
 *    "dmesg" for more info)
 * - -ENXIO is returned in case of internal error (Please, type
 *    "dmesg" for more info)
 *
 */
int a4l_sys_detach(int fd)
{
	return __sys_ioctl(fd, A4L_DEVCFG, NULL);
}

/**
 * @brief Configure the buffer size
 *
 *
 * This function can configure the buffer size of the file descriptor
 * currently in use. If the subdevice index is set to
 * A4L_BUF_DEFMAGIC, it can also define the default buffser size at
 * open time.
 *
 * @param[in] fd File descriptor as returned by a4l_sys_open()
 * @param[in] idx_subd Index of the concerned subdevice
 * @param[int] size Buffer size to be set
 *
 * @return 0 on success. Otherwise:
 *
 * - -EINVAL is returned if some argument is missing or wrong (Please,
 *    type "dmesg" for more info)
 * - -EPERM is returned if the function is called in an RT context or
 *    if the buffer to resize is mapped in user-space (Please, type
 *    "dmesg" for more info)
 * - -EFAULT is returned if a user <-> kernel transfer went wrong
 * - -EBUSY is returned if the selected subdevice is already
 *    processing an asynchronous operation
 * - -ENOMEM is returned if the system is out of memory
 *
 */
int a4l_sys_bufcfg(int fd, unsigned int idx_subd, unsigned long size)
{
	a4l_bufcfg_t cfg = { idx_subd, size };

	return __sys_ioctl(fd, A4L_BUFCFG, &cfg);
}

/** @} */

/**
 * @file
 * Analogy for Linux, command, transfer, etc. related features
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

#include <errno.h>

#include <analogy/ioctl.h>
#include <analogy/analogy.h>

#include "syscall.h"

/*!
 * @ingroup level1_lib
 * @defgroup async1_lib Asynchronous acquisition API
 * @{
 */

/**
 * @brief Send a command to an Analoy device
 *
 * The function a4l_snd_command() triggers asynchronous
 * acquisition.
 *
 * @param[in] dsc Device descriptor filled by a4l_open() (and
 * optionally a4l_fill_desc())
 * @param[in] cmd Command structure
 *
 * @return 0 on success. Otherwise:
 *
 * - -EINVAL is returned if some argument is missing or wrong (Please,
 *    type "dmesg" for more info)
 * - -ENOMEM is returned if the system is out of memory
 * - -EFAULT is returned if a user <-> kernel transfer went wrong
 * - -EIO is returned if the selected subdevice cannot handle command
 * - -EBUSY is returned if the selected subdevice is already
 *    processing an asynchronous operation
 *
 */
int a4l_snd_command(a4l_desc_t * dsc, a4l_cmd_t * cmd)
{
	/* Basic checking */
	if (dsc == NULL || dsc->fd < 0)
		return -EINVAL;

	return __sys_ioctl(dsc->fd, A4L_CMD, cmd);
}

/**
 * @brief Cancel an asynchronous acquisition
 *
 * The function a4l_snd_cancel() is devoted to stop an asynchronous
 * acquisition configured thanks to an Analogy command.
 *
 * @param[in] dsc Device descriptor filled by a4l_open() (and
 * optionally a4l_fill_desc())
 * @param[in] idx_subd Subdevice index
 *
 * @return 0 on success. Otherwise:
 *
 * - -EINVAL is returned if some argument is missing or wrong (Please,
 *    type "dmesg" for more info)
 * - -EIO is returned if the selected subdevice does not support
 *    asynchronous operation
 *
 */
int a4l_snd_cancel(a4l_desc_t * dsc, unsigned int idx_subd)
{
	/* Basic checking */
	if (dsc == NULL || dsc->fd < 0)
		return -EINVAL;

	return __sys_ioctl(dsc->fd, A4L_CANCEL, (void *)(long)idx_subd);
}

/**
 * @brief Change the size of the asynchronous buffer
 *
 * During asynchronous acquisition, a ring-buffer enables the
 * transfers from / to user-space. Functions like a4l_read() or
 * a4l_write() recovers / sends data through this intermediate
 * buffer. The function a4l_set_bufsize() can change the size of the
 * ring-buffer. Please note, there is one ring-buffer per subdevice
 * capable of asynchronous acquisition. By default, each buffer size
 * is set to 64 KB.
 *
 * @param[in] dsc Device descriptor filled by a4l_open() (and
 * optionally a4l_fill_desc())
 * @param[in] idx_subd Index of the concerned subdevice
 * @param[in] size New buffer size, the maximal tolerated value is
 * 16MB (A4L_BUF_MAXSIZE)
 *
 * @return 0 on success. Otherwise:
 *
 * - -EINVAL is returned if the analogy descriptor is not correct or
      if some argument is missing or wrong (Please, type "dmesg" for
      more info)
 * - -EPERM is returned if the function is called in an RT context or
 *    if the buffer to resize is mapped in user-space (Please, type
 *    "dmesg" for more info)
 * - -EFAULT is returned if a user <-> kernel transfer went wrong
 * - -EBUSY is returned if the selected subdevice is already
 *    processing an asynchronous operation
 * - -ENOMEM is returned if the system is out of memory
 *
 */
int a4l_set_bufsize(a4l_desc_t * dsc,
		    unsigned int idx_subd, unsigned long size)
{
	/* Basic checking */
	if (dsc == NULL || dsc->fd < 0)
		return -EINVAL;

	return a4l_sys_bufcfg(dsc->fd, idx_subd, size);
}

int a4l_set_wakesize(a4l_desc_t * dsc, unsigned long size)
{
	int err;
	a4l_bufcfg2_t cfg = { .wake_count = size };

	/* Basic checking */
	if (dsc == NULL || dsc->fd < 0)
		return -EINVAL;

	return  __sys_ioctl(dsc->fd, A4L_BUFCFG2, &cfg);
	
	return err;
}

int a4l_get_wakesize(a4l_desc_t * dsc, unsigned long *size)
{
	int err;
	a4l_bufcfg2_t cfg;

	/* Basic checking */
	if (size == NULL || dsc == NULL || dsc->fd < 0)
		return -EINVAL;

	err = __sys_ioctl(dsc->fd, A4L_BUFINFO2, &cfg);

	if (err == 0)
		*size = cfg.wake_count;

	return err;
}

/**
 * @brief Get the size of the asynchronous buffer
 *
 * During asynchronous acquisition, a ring-buffer enables the
 * transfers from / to user-space. Functions like a4l_read() or
 * a4l_write() recovers / sends data through this intermediate
 * buffer. Please note, there is one ring-buffer per subdevice
 * capable of asynchronous acquisition. By default, each buffer size
 * is set to 64 KB.
 *
 * @param[in] dsc Device descriptor filled by a4l_open() (and
 * optionally a4l_fill_desc())
 * @param[in] idx_subd Index of the concerned subdevice
 * @param[out] size Buffer size
 *
 * @return 0 on success. Otherwise:
 *
 * - -EINVAL is returned if some argument is missing or wrong (Please,
 *    type "dmesg" for more info)
 * - -EFAULT is returned if a user <-> kernel transfer went wrong
 *
 */
int a4l_get_bufsize(a4l_desc_t * dsc,
		    unsigned int idx_subd, unsigned long *size)
{
	a4l_bufinfo_t info = { idx_subd, 0, 0 };
	int ret;

	/* Basic checkings */
	if (dsc == NULL || dsc->fd < 0)
		return -EINVAL;

	if (size == NULL)
		return -EINVAL;

	ret = __sys_ioctl(dsc->fd, A4L_BUFINFO, &info);

	if (ret == 0)
		*size = info.buf_size;

	return ret;
}

/**
 * @brief Update the asynchronous buffer state
 *
 * When the mapping of the asynchronous ring-buffer (thanks to
 * a4l_mmap() is disabled, common read / write syscalls have to be
 * used.
 * In input case, a4l_read() must be used for:
 * - the retrieval of the acquired data.
 * - the notification to the Analogy layer that the acquired data have
 *   been consumed, then the area in the ring-buffer which was
 *   containing becomes available.
 * In output case, a4l_write() must be called to:
 * - send some data to the Analogy layer.
 * - signal the Analogy layer that a chunk of data in the ring-buffer
 *   must be used by the driver.

 * In mmap configuration, these features are provided by unique
 * function named a4l_mark_bufrw().
 * In input case, a4l_mark_bufrw() can :
 * - recover the count of data newly available in the ring-buffer.
 * - notify the Analogy layer how many bytes have been consumed.
 * In output case, a4l_mark_bufrw() can:
 * - recover the count of data available for writing.
 * - notify Analogy that some bytes have been written.
 *
 * @param[in] dsc Device descriptor filled by a4l_open() (and
 * optionally a4l_fill_desc())
 * @param[in] idx_subd Index of the concerned subdevice
 * @param[in] cur Amount of consumed data
 * @param[out] new Amount of available data
 *
 * @return 0 on success. Otherwise:
 *
 * - -EINVAL is returned if some argument is missing or wrong; the
 *    descriptor and the new pointer should be checked; check also the
 *    kernel log ("dmesg")
 * - -EFAULT is returned if a user <-> kernel transfer went wrong
 *
 */
int a4l_mark_bufrw(a4l_desc_t * dsc,
		   unsigned int idx_subd,
		   unsigned long cur, unsigned long *new)
{
	int ret;
	a4l_bufinfo_t info = { idx_subd, 0, cur };

	/* Basic checkings */
	if (dsc == NULL || dsc->fd < 0)
		return -EINVAL;

	if (new == NULL)
		return -EINVAL;

	ret = __sys_ioctl(dsc->fd, A4L_BUFINFO, &info);

	if (ret == 0)
		*new = info.rw_count;

	return ret;
}

/**
 * @brief Get the available data count
 *
 * @param[in] dsc Device descriptor filled by a4l_open() (and
 * optionally a4l_fill_desc())
 * @param[in] idx_subd Index of the concerned subdevice
 * @param[in] ms_timeout The number of miliseconds to wait for some
 * data to be available. Passing A4L_INFINITE causes the caller to
 * block indefinitely until some data is available. Passing
 * A4L_NONBLOCK causes the function to return immediately without
 * waiting for any available data
 *
 * @return the available data count. Otherwise:
 *
 * - -EINVAL is returned if some argument is missing or wrong (Please,
 *    type "dmesg" for more info)
 * - -EFAULT is returned if a user <-> kernel transfer went wrong
 * - -EINTR is returned if calling task has been unblocked by a signal
 *
 */
int a4l_poll(a4l_desc_t * dsc,
	     unsigned int idx_subd, unsigned long ms_timeout)
{
	int ret;
	a4l_poll_t poll = { idx_subd, ms_timeout };

	/* Basic checkings */
	if (dsc == NULL || dsc->fd < 0)
		return -EINVAL;

	ret = __sys_ioctl(dsc->fd, A4L_POLL, &poll);

	/* There is an ugly cast, but it is the only way to stick with
	   the original Comedi API */
	if (ret == 0)
		ret = (int)poll.arg;

	return ret;
}

/**
 * @brief Map the asynchronous ring-buffer into a user-space
 *
 * @param[in] dsc Device descriptor filled by a4l_open() (and
 * optionally a4l_fill_desc())
 * @param[in] idx_subd Index of the concerned subdevice
 * @param[in] size Size of the buffer to map
 * @param[out] ptr Address of the pointer containing the assigned
 * address on return
 *
 * @return 0 on success. Otherwise:
 *
 * - -EINVAL is returned if some argument is missing or wrong, the
 *    descriptor and the pointer should be checked; check also the
 *    kernel log
 * - -EPERM is returned if the function is called in an RT context or
 *    if the buffer to resize is mapped in user-space (Please, type
 *    "dmesg" for more info)
 * - -EFAULT is returned if a user <-> kernel transfer went wrong
 * - -EBUSY is returned if the buffer is already mapped in user-space
 *
 */
int a4l_mmap(a4l_desc_t * dsc,
	     unsigned int idx_subd, unsigned long size, void **ptr)
{
	int ret;
	a4l_mmap_t map = { idx_subd, size, NULL };

	/* Basic checkings */
	if (dsc == NULL || dsc->fd < 0)
		return -EINVAL;

	if (ptr == NULL)
		return -EINVAL;

	ret = __sys_ioctl(dsc->fd, A4L_MMAP, &map);

	if (ret == 0)
		*ptr = map.ptr;

	return ret;
}

/** @} Command syscall API */

/*!
 * @ingroup level2_lib
 * @defgroup async2_lib Asynchronous acquisition API
 * @{
 */

/**
 * @brief Perform asynchronous read operation on the analog input
 * subdevice
 *
 * The function a4l_async_read() is only useful for acquisition
 * configured through an Analogy command.
 *
 * @param[in] dsc Device descriptor filled by a4l_open() (and
 * optionally a4l_fill_desc())
 * @param[out] buf Input buffer
 * @param[in] nbyte Number of bytes to read
 * @param[in] ms_timeout The number of miliseconds to wait for some
 * data to be available. Passing A4L_INFINITE causes the caller to
 * block indefinitely until some data is available. Passing
 * A4L_NONBLOCK causes the function to return immediately without
 * waiting for any available data
 *
 * @return Number of bytes read, otherwise negative error code:
 *
 * - -EINVAL is returned if some argument is missing or wrong, the
 *    descriptor should be checked; check also the kernel log
 * - -ENOENT is returned if the device's reading subdevice is idle (no
 *    command was sent)
 * - -EFAULT is returned if a user <-> kernel transfer went wrong
 * - -EINTR is returned if calling task has been unblocked by a signal
 *
 */
int a4l_async_read(a4l_desc_t * dsc,
		   void *buf, size_t nbyte, unsigned long ms_timeout)
{
	/* Basic checking */
	if (dsc == NULL)
		return -EINVAL;

	/* The function a4l_poll() is useful only if
	   the timeout is not A4L_INFINITE (== 0) */
	if (ms_timeout != A4L_INFINITE) {
		int ret;

		ret = a4l_poll(dsc, dsc->idx_read_subd, ms_timeout);
		if (ret < 0)
			return ret;

		/* If the timeout value is equal to A4L_NONBLOCK,
		   there is no need to call the launch any read operation */
		if (ret == 0 && ms_timeout == A4L_NONBLOCK)
			return ret;
	}

	/* One more basic checking */
	if (dsc->fd < 0)
		return -EINVAL;

	/* Performs the read operation */
	return a4l_sys_read(dsc->fd, buf, nbyte);
}

/**
 * @brief Perform asynchronous write operation on the analog input
 * subdevice
 *
 * The function a4l_async_write() is only useful for acquisition
 * configured through an Analogy command.
 *
 * @param[in] dsc Device descriptor filled by a4l_open() (and
 * optionally a4l_fill_desc())
 * @param[in] buf Ouput buffer
 * @param[in] nbyte Number of bytes to write
 * @param[in] ms_timeout The number of miliseconds to wait for some
 * free area to be available. Passing A4L_INFINITE causes the
 * caller to block indefinitely until some data is available. Passing
 * A4L_NONBLOCK causes the function to return immediately without
 * waiting any available space to write data.
 *
 * @return Number of bytes written, otherwise negative error code:
 *
 * - -EINVAL is returned if some argument is missing or wrong, the
 *    descriptor should be checked; check also the kernel log
 * - -ENOENT is returned if the device's reading subdevice is idle (no
 *    command was sent)
 * - -EFAULT is returned if a user <-> kernel transfer went wrong
 * - -EINTR is returned if calling task has been unblocked by a signal
 *
 */
int a4l_async_write(a4l_desc_t * dsc,
		    void *buf, size_t nbyte, unsigned long ms_timeout)
{
	/* Basic checking */
	if (dsc == NULL)
		return -EINVAL;

	/* The function a4l_poll() is useful only if
	   the timeout is not A4L_INFINITE (== 0) */
	if (ms_timeout != A4L_INFINITE) {
		int ret;

		ret = a4l_poll(dsc, dsc->idx_write_subd, ms_timeout);
		if (ret < 0)
			return ret;

		/* If the timeout value is equal to A4L_NONBLOCK,
		   there is no need to call the launch any read operation */
		if (ret == 0 && ms_timeout == A4L_NONBLOCK)
			return ret;
	}

	/* One more basic checking */
	if (dsc->fd < 0)
		return -EINVAL;

	/* Performs the write operation */
	return a4l_sys_write(dsc->fd, buf, nbyte);
}

/** @} Command syscall API */

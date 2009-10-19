/**
 * @file
 * Analogy for Linux, device, subdevice, etc. related features
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

#ifndef DOXYGEN_CPP

int a4l_sys_devinfo(int fd, a4l_dvinfo_t * info)
{
	return __sys_ioctl(fd, A4L_DEVINFO, info);
}

int a4l_sys_subdinfo(int fd, a4l_sbinfo_t * info)
{
	return __sys_ioctl(fd, A4L_SUBDINFO, info);
}

int a4l_sys_nbchaninfo(int fd, unsigned int idx_subd, unsigned int *nb)
{
	int ret;
	a4l_chinfo_arg_t arg = { idx_subd, NULL };

	if (nb == NULL)
		return -EINVAL;

	ret = __sys_ioctl(fd, A4L_NBCHANINFO, &arg);
	*nb = (unsigned long)arg.info;

	return ret;
}

int a4l_sys_chaninfo(int fd, unsigned int idx_subd, a4l_chinfo_t * info)
{
	a4l_chinfo_arg_t arg = { idx_subd, info };

	return __sys_ioctl(fd, A4L_CHANINFO, &arg);
}

int a4l_sys_nbrnginfo(int fd,
		      unsigned int idx_subd,
		      unsigned int idx_chan, unsigned int *nb)
{
	int ret;
	a4l_rnginfo_arg_t arg = { idx_subd, idx_chan, NULL };

	if (nb == NULL)
		return -EINVAL;

	ret = __sys_ioctl(fd, A4L_NBRNGINFO, &arg);
	*nb = (unsigned long)arg.info;

	return ret;
}

int a4l_sys_rnginfo(int fd,
		    unsigned int idx_subd,
		    unsigned int idx_chan, a4l_rnginfo_t * info)
{
	a4l_rnginfo_arg_t arg = { idx_subd, idx_chan, info };

	return __sys_ioctl(fd, A4L_RNGINFO, &arg);
}

#endif /* !DOXYGEN_CPP */

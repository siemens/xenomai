/**
 * @file
 * Comedilib for RTDM, device, subdevice, etc. related features  
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

#ifndef DOXYGEN_CPP

int comedi_sys_devinfo(int fd, comedi_dvinfo_t * info)
{
	return __sys_ioctl(fd, COMEDI_DEVINFO, info);
}

int comedi_sys_subdinfo(int fd, comedi_sbinfo_t * info)
{
	return __sys_ioctl(fd, COMEDI_SUBDINFO, info);
}

int comedi_sys_nbchaninfo(int fd, unsigned int idx_subd, unsigned int *nb)
{
	int ret;
	comedi_chinfo_arg_t arg = { idx_subd, NULL };

	if (nb == NULL)
		return -EINVAL;

	ret = __sys_ioctl(fd, COMEDI_NBCHANINFO, &arg);
	*nb = (unsigned long)arg.info;

	return ret;
}

int comedi_sys_chaninfo(int fd, unsigned int idx_subd, comedi_chinfo_t * info)
{
	comedi_chinfo_arg_t arg = { idx_subd, info };

	return __sys_ioctl(fd, COMEDI_CHANINFO, &arg);
}

int comedi_sys_nbrnginfo(int fd,
			 unsigned int idx_subd,
			 unsigned int idx_chan, unsigned int *nb)
{
	int ret;
	comedi_rnginfo_arg_t arg = { idx_subd, idx_chan, NULL };

	if (nb == NULL)
		return -EINVAL;

	ret = __sys_ioctl(fd, COMEDI_NBRNGINFO, &arg);
	*nb = (unsigned long)arg.info;

	return ret;
}

int comedi_sys_rnginfo(int fd,
		       unsigned int idx_subd,
		       unsigned int idx_chan, comedi_rnginfo_t * info)
{
	comedi_rnginfo_arg_t arg = { idx_subd, idx_chan, info };

	return __sys_ioctl(fd, COMEDI_RNGINFO, &arg);
}

#endif /* !DOXYGEN_CPP */

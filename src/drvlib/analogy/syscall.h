/**
 * @file
 * Analogy for Linux, internal declarations
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

#ifndef __ANALOGY_LIB_INTERNAL__
#define __ANALOGY_LIB_INTERNAL__

#include <rtdm/rtdm.h>

#ifndef DOXYGEN_CPP

static inline int __sys_open(const char *fname)
{
	return rt_dev_open(fname, 0);
}

static inline int __sys_close(int fd)
{
	return rt_dev_close(fd);
}

static inline int __sys_ioctl(int fd, int request, void *arg)
{
	return rt_dev_ioctl(fd, request, arg);
}

static inline ssize_t __sys_read(int fd, void *buf, size_t nbyte)
{
	return rt_dev_read(fd, buf, nbyte);
}

static inline ssize_t __sys_write(int fd, void *buf, size_t nbyte)
{
	return rt_dev_write(fd, buf, nbyte);
}

#endif /* !DOXYGEN_CPP */

#endif /* __ANALOGY_LIB_INTERNAL__ */

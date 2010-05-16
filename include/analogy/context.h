/**
 * @file
 * Analogy for Linux, context structure / macros declarations
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

#ifndef __ANALOGY_CONTEXT__
#define __ANALOGY_CONTEXT__

#if defined(__KERNEL__) && !defined(DOXYGEN_CPP)

struct a4l_device;
struct a4l_buffer;

struct a4l_device_context {
	/* No need to hold user_info, thanks to container_of, we could
	   get it back */
	rtdm_user_info_t *user_info;

	struct a4l_device *dev; /* Which is retrieved thanks to minor
				   at open time */

	struct buffer buffer; /* The buffer field is extracted from
				 the transfer structure */
};

#endif /* __KERNEL__ && !DOXYGEN_CPP */

#endif /* __ANALOGY_CONTEXT__ */

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

#include <analogy/os_facilities.h>

struct a4l_device;

struct a4l_context {

	/* This field is redundant with the following parameters;
	   setting it at the head of the structure may save 
	   useless operations */
	struct a4l_device *dev;
	rtdm_user_info_t *user_info;
	struct rtdm_dev_context *rtdm_cxt;
};
typedef struct a4l_context a4l_cxt_t;

#define a4l_get_minor(x) ((x)->rtdm_cxt->device->device_id)

#define a4l_init_cxt(c, u, x)			\
    {							\
	(x)->rtdm_cxt = c;				\
	(x)->user_info = u;				\
	(x)->dev = NULL;				\
    }

#endif /* __KERNEL__ && !DOXYGEN_CPP */

#endif /* __ANALOGY_CONTEXT__ */

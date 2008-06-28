/**
 * @file
 * Comedi for RTDM, context structure / macros declarations
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

#ifndef __COMEDI_CONTEXT__
#define __COMEDI_CONTEXT__

#if defined(__KERNEL__) && !defined(DOXYGEN_CPP)

#include <comedi/os_facilities.h>

struct comedi_device;

struct comedi_context {

	/* This field is redundant with the following parameters;
	   setting it at the head of the structure may save 
	   useless operations */
	struct comedi_device *dev;
	rtdm_user_info_t *rtdm_usrinf;
	struct rtdm_dev_context *rtdm_cxt;
};
typedef struct comedi_context comedi_cxt_t;

#define comedi_get_minor(x) ((x)->rtdm_cxt->device->device_id)

#define comedi_init_cxt(c, u, x)			\
    {							\
	(x)->rtdm_cxt = c;				\
	(x)->rtdm_usrinf = u;				\
	(x)->dev = NULL;				\
    }

#define comedi_copy_from_user(x, d, r, s) \
    __comedi_copy_from_user(x->rtdm_usrinf, d, r, s)

#define comedi_copy_to_user(x, d, r, s) \
    __comedi_copy_to_user(x->rtdm_usrinf, d, r, s)

#endif /* __KERNEL__ && !DOXYGEN_CPP */

#endif /* __COMEDI_CONTEXT__ */

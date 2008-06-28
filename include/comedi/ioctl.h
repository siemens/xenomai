/**
 * @file
 * Comedi for RTDM, IOCTLs declarations
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

#ifndef __COMEDI_IOCTL__
#define __COMEDI_IOCTL__

#ifndef DOXYGEN_CPP

#ifdef __KERNEL__

#include <rtdm/rtdm_driver.h>

#define NB_IOCTL_FUNCTIONS 15

#endif /* __KERNEL__ */

#include <comedi/device.h>

#define CIO 'd'
#define COMEDI_DEVCFG _IOW(CIO,0,comedi_lnkdesc_t)
#define COMEDI_DEVINFO _IOR(CIO,1,comedi_dvinfo_t)
#define COMEDI_SUBDINFO _IOR(CIO,2,comedi_sbinfo_t)
#define COMEDI_CHANINFO _IOR(CIO,3,comedi_chinfo_arg_t)
#define COMEDI_RNGINFO _IOR(CIO,4,comedi_rnginfo_arg_t)
#define COMEDI_CMD _IOWR(CIO,5,comedi_cmd_t)
#define COMEDI_CANCEL _IOR(CIO,6,unsigned int)
#define COMEDI_INSNLIST _IOR(CIO,7,unsigned int)
#define COMEDI_INSN _IOR(CIO,8,unsigned int)
#define COMEDI_BUFCFG _IOR(CIO,9,comedi_bufcfg_t)
#define COMEDI_BUFINFO _IOWR(CIO,10,comedi_bufinfo_t)
#define COMEDI_POLL _IOR(CIO,11,unsigned int)
#define COMEDI_MMAP _IOWR(CIO,12,unsigned int)
#define COMEDI_NBCHANINFO _IOR(CIO,13,comedi_chinfo_arg_t)
#define COMEDI_NBRNGINFO _IOR(CIO,14,comedi_rnginfo_arg_t)

#endif /* !DOXYGEN_CPP */

#endif /* __COMEDI_IOCTL__ */

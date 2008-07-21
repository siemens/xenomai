/**
 * @file
 * Comedilib for RTDM, library facilities
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

#ifndef __COMEDI_COMEDI__
#define __COMEDI_COMEDI__

#include <unistd.h>

#include <comedi/types.h>
#include <comedi/descriptor.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DOXYGEN_CPP

/* --- Level 0 API (not supposed to be used) --- */

int comedi_sys_open(const char *fname);

int comedi_sys_close(int fd);

int comedi_sys_read(int fd, void *buf, size_t nbyte);

int comedi_sys_write(int fd, void *buf, size_t nbyte);

int comedi_sys_attach(int fd, comedi_lnkdesc_t * arg);

int comedi_sys_detach(int fd);

int comedi_sys_desc(int fd, comedi_desc_t * dsc, int pass);

int comedi_sys_devinfo(int fd, comedi_dvinfo_t * info);

int comedi_sys_subdinfo(int fd, comedi_sbinfo_t * info);

int comedi_sys_nbchaninfo(int fd, unsigned int idx_subd, unsigned int *nb);

int comedi_sys_chaninfo(int fd, unsigned int idx_subd, comedi_chinfo_t * info);

int comedi_sys_nbrnginfo(int fd,
			 unsigned int idx_subd,
			 unsigned int idx_chan, unsigned int *nb);

int comedi_sys_rnginfo(int fd,
		       unsigned int idx_subd,
		       unsigned int idx_chan, comedi_rnginfo_t * info);

/* --- Level 1 API (supposed to be used) --- */
    
int comedi_get_desc(int fd, comedi_desc_t * dsc, int pass);

int comedi_open(comedi_desc_t * dsc, const char *fname);

int comedi_close(comedi_desc_t * dsc);

int comedi_fill_desc(comedi_desc_t * dsc);

int comedi_get_subdinfo(comedi_desc_t * dsc,
			unsigned int subd, comedi_sbinfo_t ** info);

int comedi_get_chinfo(comedi_desc_t * dsc,
		      unsigned int subd,
		      unsigned int chan, comedi_chinfo_t ** info);

#define comedi_get_chan_max(x) (1ULL << (x)->nb_bits)

#define comedi_is_chan_global(x) ((x)->chan_flags & COMEDI_CHAN_GLOBAL)

int comedi_get_rnginfo(comedi_desc_t * dsc,
		       unsigned int subd,
		       unsigned int chan,
		       unsigned int rng, comedi_rnginfo_t ** info);

#define comedi_is_rng_global(x) ((x)->flags & COMEDI_RNG_GLOBAL)

int comedi_snd_command(comedi_desc_t * dsc, comedi_cmd_t * cmd);
    
int comedi_snd_cancel(comedi_desc_t * dsc, unsigned int idx_subd);

int comedi_set_bufsize(comedi_desc_t * dsc,
			       unsigned int idx_subd, unsigned long size);

int comedi_get_bufsize(comedi_desc_t * dsc,
		       unsigned int idx_subd, unsigned long *size);

int comedi_mark_bufrw(comedi_desc_t * dsc,
		      unsigned int idx_subd,
		      unsigned long cur, unsigned long *newp);

int comedi_poll(comedi_desc_t * dsc,
		unsigned int idx_subd, unsigned long ms_timeout);
    
int comedi_mmap(comedi_desc_t * dsc,
		unsigned int idx_subd, unsigned long size, void **ptr);

int comedi_snd_insnlist(comedi_desc_t * dsc, comedi_insnlst_t * arg);

int comedi_snd_insn(comedi_desc_t * dsc, comedi_insn_t * arg);

/* --- Level 2 API (supposed to be used) --- */

int comedi_sync_write(comedi_desc_t * dsc,
		      unsigned int idx_subd,
		      unsigned int chan_desc,
		      unsigned int delay, void *buf, size_t nbyte);

int comedi_sync_read(comedi_desc_t * dsc,
		     unsigned int idx_subd,
		     unsigned int chan_desc,
		     unsigned int delay, void *buf, size_t nbyte);
    
int comedi_find_range(comedi_desc_t * dsc,
		      unsigned int idx_subd,
		      unsigned int idx_chan,
		      unsigned long unit,
		      double min, double max, comedi_rnginfo_t ** rng);

int comedi_to_phys(comedi_chinfo_t * chan,
		   comedi_rnginfo_t * rng, double *dst, void *src,
		   int cnt);
    
int comedi_from_phys(comedi_chinfo_t * chan,
		     comedi_rnginfo_t * rng, void *dst, double *src,
		     int cnt);

#endif /* !DOXYGEN_CPP */

#ifdef __cplusplus
}
#endif
#endif /* __COMEDI_COMEDI__ */

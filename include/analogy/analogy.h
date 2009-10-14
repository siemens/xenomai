/**
 * @file
 * Analogy, library facilities
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

#ifndef __ANALOGY_ANALOGY__
#define __ANALOGY_ANALOGY__

#include <unistd.h>

#include <analogy/types.h>
#include <analogy/descriptor.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DOXYGEN_CPP

/* --- Level 0 API (not supposed to be used) --- */

int analogy_sys_open(const char *fname);

int analogy_sys_close(int fd);

int analogy_sys_read(int fd, void *buf, size_t nbyte);

int analogy_sys_write(int fd, void *buf, size_t nbyte);

int analogy_sys_attach(int fd, analogy_lnkdesc_t * arg);

int analogy_sys_detach(int fd);

int analogy_sys_desc(int fd, analogy_desc_t * dsc, int pass);

int analogy_sys_devinfo(int fd, analogy_dvinfo_t * info);

int analogy_sys_subdinfo(int fd, analogy_sbinfo_t * info);

int analogy_sys_nbchaninfo(int fd, unsigned int idx_subd, unsigned int *nb);

int analogy_sys_chaninfo(int fd, 
			 unsigned int idx_subd, analogy_chinfo_t * info);

int analogy_sys_nbrnginfo(int fd,
			  unsigned int idx_subd,
			  unsigned int idx_chan, unsigned int *nb);
	
int analogy_sys_rnginfo(int fd,
			unsigned int idx_subd,
			unsigned int idx_chan, analogy_rnginfo_t * info);

/* --- Level 1 API (supposed to be used) --- */
    
int analogy_get_desc(int fd, analogy_desc_t * dsc, int pass);

int analogy_open(analogy_desc_t * dsc, const char *fname);

int analogy_close(analogy_desc_t * dsc);

int analogy_fill_desc(analogy_desc_t * dsc);

int analogy_get_subdinfo(analogy_desc_t * dsc,
			 unsigned int subd, analogy_sbinfo_t ** info);

int analogy_get_chinfo(analogy_desc_t * dsc,
		       unsigned int subd,
		       unsigned int chan, analogy_chinfo_t ** info);

#define analogy_get_chan_max(x) (1ULL << (x)->nb_bits)

#define analogy_is_chan_global(x) ((x)->chan_flags & A4L_CHAN_GLOBAL)

int analogy_get_rnginfo(analogy_desc_t * dsc,
			unsigned int subd,
			unsigned int chan,
			unsigned int rng, analogy_rnginfo_t ** info);

#define analogy_is_rng_global(x) ((x)->flags & A4L_RNG_GLOBAL)

int analogy_snd_command(analogy_desc_t * dsc, analogy_cmd_t * cmd);
    
int analogy_snd_cancel(analogy_desc_t * dsc, unsigned int idx_subd);

int analogy_set_bufsize(analogy_desc_t * dsc,
			unsigned int idx_subd, unsigned long size);

int analogy_get_bufsize(analogy_desc_t * dsc,
			unsigned int idx_subd, unsigned long *size);

int analogy_mark_bufrw(analogy_desc_t * dsc,
		       unsigned int idx_subd,
		       unsigned long cur, unsigned long *newp);

int analogy_poll(analogy_desc_t * dsc,
		 unsigned int idx_subd, unsigned long ms_timeout);
    
int analogy_mmap(analogy_desc_t * dsc,
		unsigned int idx_subd, unsigned long size, void **ptr);

int analogy_snd_insnlist(analogy_desc_t * dsc, analogy_insnlst_t * arg);

int analogy_snd_insn(analogy_desc_t * dsc, analogy_insn_t * arg);

/* --- Level 2 API (supposed to be used) --- */

int analogy_sync_write(analogy_desc_t * dsc,
		       unsigned int idx_subd,
		       unsigned int chan_desc,
		       unsigned int delay, void *buf, size_t nbyte);

int analogy_sync_read(analogy_desc_t * dsc,
		      unsigned int idx_subd,
		      unsigned int chan_desc,
		      unsigned int delay, void *buf, size_t nbyte);
    
int analogy_find_range(analogy_desc_t * dsc,
		       unsigned int idx_subd,
		       unsigned int idx_chan,
		       unsigned long unit,
		       double min, double max, analogy_rnginfo_t ** rng);

int analogy_to_phys(analogy_chinfo_t * chan,
		    analogy_rnginfo_t * rng, double *dst, void *src,
		    int cnt);
    
int analogy_from_phys(analogy_chinfo_t * chan,
		      analogy_rnginfo_t * rng, void *dst, double *src,
		      int cnt);

#endif /* !DOXYGEN_CPP */

#ifdef __cplusplus
}
#endif
#endif /* __ANALOGY_ANALOGY__ */

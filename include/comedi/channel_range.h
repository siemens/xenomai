/**
 * @file
 * Comedi for RTDM, channel, range related features
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

#ifndef __COMEDI_CHANNEL_RANGE__
#define __COMEDI_CHANNEL_RANGE__

#ifndef DOXYGEN_CPP

#if __GNUC__ >= 3
#define GCC_ZERO_LENGTH_ARRAY
#else
#define GCC_ZERO_LENGTH_ARRAY 0
#endif

/* --- Channel section --- */

#define COMEDI_CHAN_AREF_GROUND 0x1
#define COMEDI_CHAN_AREF_COMMON 0x2
#define COMEDI_CHAN_AREF_DIFF 0x4
#define COMEDI_CHAN_AREF_OTHER 0x8
#define COMEDI_CHAN_GLOBAL 0x10

/* Per channel descriptor */
struct comedi_channel_features {
	unsigned long flags;
	unsigned char nb_bits;
};
typedef struct comedi_channel_features comedi_chfeats_t;

#define COMEDI_CHAN_GLOBAL_CHANDESC 0
#define COMEDI_CHAN_PERCHAN_CHANDESC 1

/* Channels descriptor */
struct comedi_channels_desc {
	unsigned char mode;
	unsigned char length;
	comedi_chfeats_t chans[GCC_ZERO_LENGTH_ARRAY];
};
typedef struct comedi_channels_desc comedi_chdesc_t;

/* --- Range section --- */

#define COMEDI_RNG_FACTOR 1000000

#define COMEDI_RNG_VOLT_UNIT 0x0
#define COMEDI_RNG_MAMP_UNIT 0x1
#define COMEDI_RNG_NO_UNIT 0x2
#define COMEDI_RNG_UNIT(x) (x & (COMEDI_RNG_VOLT_UNIT | \
				 COMEDI_RNG_MAMP_UNIT | \
				 COMEDI_RNG_NO_UNIT))
#define COMEDI_RNG_GLOBAL 0x4

/* Per range descriptor */
struct comedi_range_features {
	long min;
	long max;
	unsigned long flags;
};
typedef struct comedi_range_features comedi_rngfeats_t;

#define RANGE(x,y) {(x * COMEDI_RNG_FACTOR), (y * COMEDI_RNG_FACTOR),	\
	    COMEDI_RNG_NO_UNIT}
#define RANGE_V(x,y) {(x * COMEDI_RNG_FACTOR),(y * COMEDI_RNG_FACTOR),	\
	    COMEDI_RNG_VOLT_UNIT}
#define RANGE_mA(x,y) {(x * COMEDI_RNG_FACTOR),(y * COMEDI_RNG_FACTOR), \
	    COMEDI_RNG_MAMP_UNIT}

/* Ranges tab descriptor */
#define COMEDI_RNGTAB(x) \
struct { \
    unsigned char length;  \
    comedi_rngfeats_t rngs[x]; \
}
typedef COMEDI_RNGTAB(GCC_ZERO_LENGTH_ARRAY) comedi_rngtab_t;

#define COMEDI_RNG_GLOBAL_RNGDESC 0
#define COMEDI_RNG_PERCHAN_RNGDESC 1

/* Global ranges descriptor */
#define COMEDI_RNGDESC(x) \
struct { \
    unsigned char mode; \
    unsigned char length; \
    comedi_rngtab_t *rngtabs[x]; \
}
typedef COMEDI_RNGDESC(GCC_ZERO_LENGTH_ARRAY) comedi_rngdesc_t;

#define RNG_GLOBAL(x) { \
    mode: COMEDI_RNG_GLOBAL_RNGDESC, \
    length: 1, \
    rngtabs: {&(x)}, }

extern comedi_rngdesc_t range_bipolar10;
extern comedi_rngdesc_t range_bipolar5;
extern comedi_rngdesc_t range_unipolar10;
extern comedi_rngdesc_t range_unipolar5;

#endif /* !DOXYGEN_CPP */

#endif /* __COMEDI_CHANNEL_RANGE__ */

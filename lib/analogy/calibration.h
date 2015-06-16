/**
 * @file
 * Analogy for Linux, internal calibration declarations
 *
 * @note Copyright (C) 2014 Jorge A Ramirez-Ortiz <jro@xenomai.org>
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

#ifndef __ANALOGY_CALIBRATION_H__
#define __ANALOGY_CALIBRATION_H__

/*
 * internal definitions between the xenomai utils and the library.
 * no need to expose them to the USER
 *
 */
#define ELEMENT_FIELD_FMT	"%s_%d:%s"
#define ELEMENT_FMT		"%s:%s"
#define COEFF_FMT		ELEMENT_FIELD_FMT"_%d"

#define PLATFORM_STR		"platform"
#define CALIBRATION_SUBD_STR	"calibration"
#define MEMORY_SUBD_STR		"memory"
#define AI_SUBD_STR		"analog_input"
#define AO_SUBD_STR		"analog_output"

#define INDEX_STR	"index"
#define ELEMENTS_STR	"elements"
#define CHANNEL_STR	"channel"
#define RANGE_STR	"range"
#define EXPANSION_STR	"expansion_origin"
#define NBCOEFF_STR	"nbcoeff"
#define COEFF_STR	"coeff"
#define BOARD_STR	"board_name"
#define DRIVER_STR	"driver_name"

struct polynomial {
	double expansion_origin;
	double *coefficients;
	int nb_coefficients;
	int order;
};

struct subdevice_calibration_node {
	struct holder node;
	struct polynomial *polynomial;
	unsigned channel;
	unsigned range;
};

void write_calibration_file(FILE *dst, struct listobj *l,
                            struct a4l_calibration_subdev *subd,
	                    a4l_desc_t *desc);

#endif

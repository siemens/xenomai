/*
 * Analogy for Linux, NI - M calibration program
 *
 * Copyright (C) 2014 Jorge A. Ramirez-Ortiz <jro@xenomai.org>
 *
 * from original code from the Comedi project
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

#ifndef __NI_M_SOFTWARE_CALIBRATE_H__
#define __NI_M_SOFTWARE_CALIBRATE_H__
#include <rtdm/uapi/analogy.h>
#include "calibration.h"
#include "analogy_calibrate.h"
#include "boilerplate/list.h"

extern const char *ni_m_boards[];
extern const int nr_ni_m_boards;

#define ni_m_board_supported(id) __array_search(id, ni_m_boards, nr_ni_m_boards)

int ni_m_software_calibrate(FILE *p);

#define init_interface(a, b)  a = ((typeof(a)) INIT_##b);

#define	 SET_BIT(n,set)	do { *(set) |= (1 << n); } while(0)

/*
 * subdevice
 */
#define SET_SUBD(type, a, b, c)			\
         do {    type##_subd.idx = a;		\
	         type##_subd.info = b;		\
		 type##_subd.name = c;		\
         } while (0)

#define INIT_SUBD 				\
{						\
        .slen = 0,				\
        .idx = -1,				\
	.info = NULL,				\
	.name = NULL,				\
}

/*
 * eeprom
 */
#define INIT_EEPROM_OPS								\
{										\
	.get_calibration_base_address = eeprom_get_calibration_base_address,	\
	.read_reference_voltage = eeprom_read_reference_voltage,		\
	.read_uint16 = eeprom_read_uint16,					\
	.read_float = eeprom_read_float,					\
	.read_byte = eeprom_read_byte,						\
}

#define INIT_EEPROM 			\
{					\
        .ops = INIT_EEPROM_OPS,		\
	.voltage_ref_offset = 12,	\
}

typedef int (*eeprom_get_calibration_base_address_function)(unsigned *);
typedef int (*eeprom_read_uint16_function)(unsigned, unsigned *);
typedef int (*eeprom_read_byte_function)(unsigned, unsigned *);
typedef int (*eeprom_read_reference_voltage_function)(float *);
typedef int (*eeprom_read_float_function)(unsigned, float *);

struct eeprom_ops {
	eeprom_get_calibration_base_address_function get_calibration_base_address;
	eeprom_read_reference_voltage_function read_reference_voltage;
	eeprom_read_uint16_function read_uint16;
	eeprom_read_float_function read_float;
	eeprom_read_byte_function read_byte;
};

struct eeprom {
	struct eeprom_ops ops;
	int voltage_ref_offset;
};

/*
 * subdevice operations
 */
#define INIT_SUBDEV_DATA_OPS		\
{					\
	.read_async = data_read_async,	\
        .read_hint = data_read_hint,	\
	.read = data_read,		\
	.write = data_write,		\
}

#define INIT_SUBDEV_OPS			\
{					\
        .data = INIT_SUBDEV_DATA_OPS	\
}

typedef int (*data_read_async_function)(void *, struct a4l_calibration_subdev *, unsigned , int , int);
typedef int (*data_read_hint_function)(struct a4l_calibration_subdev *s, int, int, int);
typedef int (*data_read_function)(unsigned *, struct a4l_calibration_subdev *, int, int, int);
typedef int (*data_write_function)(long int *, struct a4l_calibration_subdev *s, int, int, int);

struct subdev_ops {
	struct data_ops {
		data_read_async_function read_async;
		data_read_hint_function read_hint;
		data_write_function write;
		data_read_function read;
	} data;
};


/*
 * gnumath
 */
#define INIT_GNU_MATH_STATS						\
{									\
	.stddev_of_mean = statistics_standard_deviation_of_mean,	\
	.stddev = statistics_standard_deviation,			\
        .mean = statistics_mean,					\
}

#define INIT_GNU_MATH_POLYNOMIAL		\
{						\
	.fit = polynomial_fit,			\
	.linearize = polynomial_linearize,	\
}

#define INIT_GNU_MATH				\
{						\
        .stats = INIT_GNU_MATH_STATS,		\
	.polynomial = INIT_GNU_MATH_POLYNOMIAL,	\
}


struct codes {
	double measured;
	double nominal;
};

struct codes_info {
	struct codes *codes;
	int nb_codes;
};

typedef void (*statistics_standard_deviation_of_mean_function)(double *, double [], int, double );
typedef void (*statistics_standard_deviation_function)(double *, double [], int, double);
typedef void (*statistics_mean_function)(double *, double [], int);

struct statistics_ops {
	statistics_standard_deviation_of_mean_function stddev_of_mean;
	statistics_standard_deviation_function stddev;
	statistics_mean_function mean;
};


typedef int (*polynomial_linearize_function) (double *, struct polynomial *, double);
typedef int (*polynomial_fit_function)(struct polynomial *, struct codes_info *);
struct polynomial_ops {
	polynomial_fit_function fit;
	polynomial_linearize_function linearize;
};

struct gnumath {
	struct statistics_ops stats;
	struct polynomial_ops polynomial;
};

/*
 * reference
 */
#define	positive_cal_shift  	 7
#define	negative_cal_shift 	 10
#define	REF_POS_CAL  		(2 << positive_cal_shift)
#define	REF_POS_CAL_PWM_500mV  	(3 << positive_cal_shift)
#define	REF_POS_CAL_PWM_2V	(4 << positive_cal_shift)
#define	REF_POS_CAL_PWM_10V 	(5 << positive_cal_shift)
#define	REF_POS_CAL_GROUND	(6 << positive_cal_shift)
#define	REF_POS_CAL_AO 		(7 << positive_cal_shift)

#define	REF_NEG_CAL_1V		(2 << negative_cal_shift)
#define	REF_NEG_CAL_1mV 	(3 << negative_cal_shift)
#define	REF_NEG_CAL_GROUND	(5 << negative_cal_shift)
#define	REF_NEG_CAL_GROUND2 	(6 << negative_cal_shift)
#define	REF_NEG_CAL_PWM_10V 	(7 << negative_cal_shift)

#define INIT_REFERENCES 					\
{								\
	.get_min_speriod = reference_get_min_sampling_period,	\
	.set_bits = reference_set_bits,				\
	.set_pwm = reference_set_pwm,				\
	.read_samples = reference_read_samples,			\
	.read_doubles = reference_read_doubles,			\
}

typedef int (*reference_set_pwm_function)(struct a4l_calibration_subdev *s, unsigned, unsigned, unsigned *, unsigned *);
typedef int (*reference_read_reference_doubles_function)(double [], unsigned, int, int);
typedef int (*reference_read_reference_samples_function)(void *, unsigned, int, int);
typedef int (*reference_get_min_sampling_period_function)(int *);
typedef int (*reference_set_reference_channel_function)(void);
typedef int (*reference_set_reference_src_function)(void);
typedef int (*reference_set_bits_function)(unsigned);

struct references {
	reference_set_reference_src_function set_src;
	reference_set_reference_channel_function set_chan;
	reference_set_pwm_function set_pwm;
	reference_read_reference_samples_function read_samples;
	reference_read_reference_doubles_function read_doubles;
	reference_get_min_sampling_period_function get_min_speriod;
	/* private */
	reference_set_bits_function set_bits;

};

struct characterization_node {
	double mean;
	int up_tick;
};

struct pwm_info {
	struct characterization_node *node;
	unsigned nb_nodes;
};

/*
 * NI M calibrator data
 */

#define NI_M_MIN_PWM_PULSE_TICKS	( 0x20 )
#define NI_M_MASTER_CLOCK_PERIOD	( 50 )
#define NI_M_TARGET_PWM_PERIOD_TICKS	( 20 * NI_M_MIN_PWM_PULSE_TICKS )
#define NI_M_NR_SAMPLES			( 15000 )
#define NI_M_BASE_RANGE			( 0 )


struct calibrated_ranges {
	unsigned *ranges;
	unsigned nb_ranges;
};

#define ALL_CHANNELS	0xFFFFFFFF
#define ALL_RANGES	0xFFFFFFFF



#endif

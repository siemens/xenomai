/**
 * @file
 * Analogy for Linux, calibration program
 *
 * @note Copyright (C) 2014 Jorge A. Ramirez-Ortiz <jro@xenomai.org>
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


#ifndef __ANALOGY_CALIBRATE_H__
#define __ANALOGY_CALIBRATE_H__

#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include "iniparser/iniparser.h"
#include "git-stamp.h"
#include "error.h"

extern struct timespec calibration_start_time;
extern a4l_desc_t descriptor;
extern FILE *cal;

struct apply_calibration_params {
	int channel;
	char *name;
	int range;
	int subd;
	int aref;
};

extern struct apply_calibration_params params;

#define ARRAY_LEN(a)  (sizeof(a) / sizeof((a)[0]))

#define RETURN	 1
#define CONT 	 0
#define EXIT	-1

#define error(action, code, fmt, ...) 						\
do {										\
       error_at_line(action, code, __FILE__, __LINE__, fmt, ##__VA_ARGS__ );	\
       if (action == RETURN) 							\
               return -1;							\
}while(0)

struct breakdown_time {
	int ms;
	int us;
	int ns;
};

static inline void do_time_breakdown(struct breakdown_time *p,
				     const struct timespec *t)
{
	unsigned long long ms, us, ns;

	ns = t->tv_sec * 1000000000ULL;
	ns += t->tv_nsec;
	ms = ns / 1000000ULL;
	us = (ns % 1000000ULL) / 1000ULL;

	p->ms = (int)ms;
	p->us = (int)us;
	p->ns = (int)ns;
}

static inline void timespec_sub(struct timespec *r,
				const struct timespec *__restrict t1,
				const struct timespec *__restrict t2)
{
	r->tv_sec = t1->tv_sec - t2->tv_sec;
	r->tv_nsec = t1->tv_nsec - t2->tv_nsec;
	if (r->tv_nsec < 0) {
		r->tv_sec--;
		r->tv_nsec += 1000000000;
	}
}

static inline void __debug(char *fmt, ...)
{
	struct timespec now, delta;
	struct breakdown_time tm;
	char *header, *msg;
	int hlen, mlen;
	va_list ap;
	__attribute__((unused)) int err;

	va_start(ap, fmt);

	clock_gettime(CLOCK_MONOTONIC, &now);
	timespec_sub(&delta, &now, &calibration_start_time);
	do_time_breakdown(&tm, &delta);

	hlen = asprintf(&header, "%4d\"%.3d.%.3d| ",
			tm.ms / 1000, tm.ms % 1000, tm.us);

	mlen = vasprintf(&msg, fmt, ap);

	err = write(fileno(stdout), header, hlen);
	err = write(fileno(stdout), msg, mlen);

	free(header);
	free(msg);

	va_end(ap);
}

static inline void __push_to_file(FILE *file, char *fmt, ...)
{
	va_list ap;

	if (!file)
		return;

	va_start(ap, fmt);
	vfprintf(file, fmt, ap);
	fflush(file);
	va_end(ap);
}

static inline int __array_search(char *elem, const char *array[], int len)
{
	int i;

	for (i = 0; i < len; i++)
		if (strncmp(elem, array[i], strlen(array[i])) == 0)
			return 0;

	return -1;
}

#define push_to_cal_file(fmt, ...) 						\
        do {									\
                if (cal)							\
                        __push_to_file(cal, fmt, ##__VA_ARGS__);		\
	} while(0)

static inline double rng_max(a4l_rnginfo_t *range)
{
	double a, b;
	b = A4L_RNG_FACTOR * 1.0;

	a = (double) range->max;
	a = a / b;
	return a;
}

static inline double rng_min(a4l_rnginfo_t *range)
{
	double a, b;

	b = A4L_RNG_FACTOR * 1.0;
	a = (double) range->min;
	a = a / b;
	return a;
}

struct subd_data {
	int index;
	int channel;
	int range;
	int expansion;
	int nb_coeff;
	double *coeff;
};

struct calibration_data {
	char *driver_name;
	char *board_name;
	int nb_ai;
	struct subd_data *ai;
	int nb_ao;
	struct subd_data *ao;
};

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

static inline int
read_calfile_str(char ** val, struct _dictionary_ *f, const char *subd, char *type)
{
	char *not_found = NULL;
	char *str;
	int err;

	err = asprintf(&str, ELEMENT_FMT, subd, type);
	if (err < 0)
		error(EXIT, 0, "asprintf \n");
	*val = (char *) iniparser_getstring(f, str, not_found);
	__debug("%s = %s \n", str, *val);
	free(str);
	if (*val == not_found)
		error(EXIT, 0, "calibration file: str element not found \n");

	return 0;
}


static inline int
read_calfile_integer(int *val, struct _dictionary_ *f,
		    const char *subd, int subd_idx, char *type)
{
	int not_found = 0xFFFF;
	char *str;
	int err;

	if (subd_idx < 0)
		err = asprintf(&str, ELEMENT_FMT, subd, type);
	else
		err = asprintf(&str, ELEMENT_FIELD_FMT, subd, subd_idx, type);
	if (err < 0)
		error(EXIT, 0, "asprintf \n");
	*val = iniparser_getint(f, str, not_found);
	__debug("%s = %d \n", str, *val);
	free(str);
	if (*val == not_found)
		error(EXIT, 0, "calibration file: int element not found \n");

	return 0;
}

static inline int
read_calfile_double(double *d, struct _dictionary_ *f,
		    const char *subd, int subd_idx, char *type, int type_idx)
{
	const double not_found = -255.0;
	char *str;
	int err;

	if (strncmp(type, COEFF_STR, strlen(COEFF_STR) != 0))
		error(EXIT, 0, "only contains doubles as coefficients \n");
	err = asprintf(&str, COEFF_FMT, subd, subd_idx, type, type_idx);
	if (err < 0)
		error(EXIT, 0, "asprintf \n");
	*d = iniparser_getdouble(f, str, not_found);
	__debug("%s = %g \n", str, *d);
	free(str);
	if (*d == not_found)
		error(EXIT, 0, "calbration file: double element not found \n");

	return 0;
}



#endif

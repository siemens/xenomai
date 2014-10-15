/**
 * @file
 * Analogy for Linux, device, subdevice, etc. related features
 *
 * @note Copyright (C) 1997-2000 David A. Schleef <ds@schleef.org>
 * @note Copyright (C) 2014 Jorge A. Ramirez-Ortiz <jro@xenomai.org>
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

#include <rtdm/analogy.h>
#include <stdio.h>
#include <errno.h>
#include <wordexp.h>
#include "iniparser/iniparser.h"
#include "boilerplate/list.h"
#include "calibration.h"

#define ARRAY_LEN(a)  (sizeof(a) / sizeof((a)[0]))

static inline int read_dbl(double *d, struct _dictionary_ *f,const char *subd,
			   int subd_idx, char *type, int type_idx)
{
	char *str;
	int ret;

	/* if only contains doubles as coefficients */
	if (strncmp(type, COEFF_STR, strlen(COEFF_STR) != 0))
		return -ENOENT;

	ret = asprintf(&str, COEFF_FMT, subd, subd_idx, type, type_idx);
	if (ret < 0)
		return ret;

	*d = iniparser_getdouble(f, str, -255.0);
	if (*d == -255.0)
		ret = -ENOENT;
	free(str);

	return ret;
}

static inline int read_int(int *val, struct _dictionary_ *f, const char *subd,
			   int subd_idx, char *type)
{
	char *str;
	int ret;

	ret = (subd_idx >= 0) ?
	      asprintf(&str, ELEMENT_FIELD_FMT, subd, subd_idx, type):
	      asprintf(&str, ELEMENT_FMT, subd, type);
	if (ret < 0)
		return ret;

	*val = iniparser_getint(f, str, 0xFFFF);
	if (*val == 0xFFFF)
		ret = -ENOENT;
	free(str);

	return ret;
}

static inline int read_str(char **val, struct _dictionary_ *f, const char *subd,
	                   const char *type)
{
	char *str;
	int ret;

	ret = asprintf(&str, ELEMENT_FMT, subd, type);
	if (ret < 0)
		return ret;

	*val = (char *) iniparser_getstring(f, str, NULL);
	if (*val == NULL)
		ret = -ENOENT;
	free(str);

	return ret;
}

static inline void write_calibration(FILE *file, char *fmt, ...)
{
	va_list ap;

	if (!file)
		return;

	va_start(ap, fmt);
	vfprintf(file, fmt, ap);
	fflush(file);
	va_end(ap);
}

int a4l_read_calibration_file(char *name, struct a4l_calibration_data *data)
{
	const char *subdevice[2] = { AI_SUBD_STR, AO_SUBD_STR };
	struct a4l_calibration_subdev_data *p = NULL;
	int i, j, k, index = -1, nb_elements = -1;
	struct _dictionary_ *d;
	const char *filename;
	wordexp_t exp;
	int ret = 0;

	ret = wordexp(name, &exp, WRDE_NOCMD|WRDE_UNDEF);
	if (ret) {
		/* can't apply calibration */
		ret = ret == WRDE_NOSPACE ? -ENOMEM : -EINVAL;
		return ret;
	}

	if (exp.we_wordc != 1) {
		/* "weird expansion of %s as rc file \n", params.name */
		return -1;
	}

	filename = exp.we_wordv[0];
	if (access(filename, R_OK)) {
		/* "cant access %s for reading \n", params.name */
		return -1;
	}

	d = iniparser_load(filename);
	if (d == NULL) {
		/* "loading error for %s (%d)\n", params.name, errno */
		return -1;
	}

	read_str(&data->driver_name, d, PLATFORM_STR, DRIVER_STR);
	read_str(&data->board_name, d, PLATFORM_STR, BOARD_STR);

	for (k = 0; k < ARRAY_LEN(subdevice); k++) {
		read_int(&nb_elements, d, subdevice[k], -1, ELEMENTS_STR);
		read_int(&index, d, subdevice[k], -1, INDEX_STR);

		if (strncmp(subdevice[k], AI_SUBD_STR,
			    strlen(AI_SUBD_STR)) == 0) {
			data->ai = malloc(nb_elements *
				   sizeof(struct a4l_calibration_subdev_data));
			data->nb_ai = nb_elements;
			p  = data->ai;
		}

		if (strncmp(subdevice[k], AO_SUBD_STR,
			    strlen(AO_SUBD_STR)) == 0) {
			data->ao = malloc(nb_elements *
				   sizeof(struct a4l_calibration_subdev_data));
			data->nb_ao = nb_elements;
			p = data->ao;
		}

		for (i = 0; i < nb_elements; i++) {
			read_int(&p->expansion, d, subdevice[k], i,
				 EXPANSION_STR);
			read_int(&p->nb_coeff, d, subdevice[k], i,
				 NBCOEFF_STR);
			read_int(&p->channel, d, subdevice[k], i,
				 CHANNEL_STR);
			read_int(&p->range, d, subdevice[k], i,
				 RANGE_STR);

			p->coeff = malloc(p->nb_coeff * sizeof(double));

			for (j = 0; j < p->nb_coeff; j++) {
				read_dbl(&p->coeff[j], d, subdevice[k], i,
					COEFF_STR, j);
			}

			p->index = index;
			p++;
		}
	}
	wordfree(&exp);

	return 0;
}

void a4l_write_calibration_file(FILE *dst, struct list *l,
	                        struct a4l_calibration_subdev *subd,
	                        a4l_desc_t *desc)
{
	struct subdevice_calibration_node *e, *t;
	int i, j = 0;

	if (list_empty(l))
		return;

	/* TODO: modify the meaning of board/driver in the proc */
	if (desc) {
		write_calibration(dst, "[%s] \n",PLATFORM_STR);
		write_calibration(dst, DRIVER_STR" = %s;\n", desc->board_name);
		write_calibration(dst, BOARD_STR" = %s;\n", desc->driver_name);
	}

	write_calibration(dst, "\n[%s] \n", subd->name);
	write_calibration(dst, INDEX_STR" = %d;\n", subd->idx);
	list_for_each_entry_safe(e, t, l, node) {
		j++;
	}
	write_calibration(dst, ELEMENTS_STR" = %d;\n", j);

	j = 0;
	list_for_each_entry_safe(e, t, l, node) {
		write_calibration(dst, "[%s_%d] \n", subd->name, j);
		write_calibration(dst, CHANNEL_STR" = %d;\n", e->channel);
		write_calibration(dst, RANGE_STR" = %d;\n", e->range);
		write_calibration(dst, EXPANSION_STR" = %g;\n",
				  e->polynomial->expansion_origin);
		write_calibration(dst, NBCOEFF_STR"= %d;\n",
				  e->polynomial->nb_coefficients);

		for (i = 0; i < e->polynomial->nb_coefficients; i++)
			write_calibration(dst, COEFF_STR"_%d = %g;\n",
					  i,
					  e->polynomial->coefficients[i]);
		j++;
	}

	return;
}



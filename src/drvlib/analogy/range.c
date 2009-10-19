/**
 * @file
 * Analogy for Linux, range related features
 *
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

#include <errno.h>
#include <math.h>

#include <analogy/analogy.h>

#ifndef DOXYGEN_CPP

lsampl_t data32_get(void *src)
{
	return (lsampl_t) * ((lsampl_t *) (src));
}

lsampl_t data16_get(void *src)
{
	return (lsampl_t) * ((sampl_t *) (src));
}

lsampl_t data8_get(void *src)
{
	return (lsampl_t) * ((unsigned char *)(src));
}

void data32_set(void *dst, lsampl_t val)
{
	*((lsampl_t *) (dst)) = val;
}

void data16_set(void *dst, lsampl_t val)
{
	*((sampl_t *) (dst)) = (sampl_t) (0xffff & val);
}

void data8_set(void *dst, lsampl_t val)
{
	*((unsigned char *)(dst)) = (unsigned char)(0xff & val);
}

#endif /* !DOXYGEN_CPP */

/*!
 * @ingroup level2_lib
 * @defgroup rng2_lib Range / conversion API
 * @{
 */

/**
 * @brief Find the must suitable range
 *
 * @param[in] dsc Device descriptor filled by a4l_open() and
 * a4l_fill_desc()
 *
 * @param[in] idx_subd Index of the concerned subdevice
 * @param[in] idx_chan Index of the concerned channel
 * @param[in] unit Unit type used in the range
 * @param[in] min Minimal limit value
 * @param[in] max Maximal limit value
 * @param[out] rng Found range
 *
 * @return 0 on success, otherwise a negative error code.
 *
 */
int a4l_find_range(a4l_desc_t * dsc,
		   unsigned int idx_subd,
		   unsigned int idx_chan,
		   unsigned long unit,
		   double min, double max, a4l_rnginfo_t ** rng)
{
	int i, ret;
	long lmin, lmax;
	a4l_chinfo_t *chinfo;
	a4l_rnginfo_t *rnginfo;

	/* Basic checkings */
	if (dsc == NULL || rng == NULL)
		return -EINVAL;

	/* a4l_fill_desc() must have been called on this descriptor */
	if (dsc->magic != MAGIC_CPLX_DESC)
		return -EINVAL;

	/* Retrieves the ranges count */
	ret = a4l_get_chinfo(dsc, idx_subd, idx_chan, &chinfo);
	if (ret < 0)
		goto out_get_range;

	/* Initializes variables */
	lmin = (long)(min * A4L_RNG_FACTOR);
	lmax = (long)(max * A4L_RNG_FACTOR);
	*rng = NULL;

	/* Performs the research */
	for (i = 0; i < chinfo->nb_rng; i++) {

		ret = a4l_get_rnginfo(dsc, idx_subd, idx_chan, i, &rnginfo);
		if (ret < 0)
			goto out_get_range;

		if (A4L_RNG_UNIT(rnginfo->flags) == unit &&
		    rnginfo->min <= lmin && rnginfo->max >= lmax) {

			if (*rng != NULL) {
				if (rnginfo->min >= (*rng)->min &&
				    rnginfo->max <= (*rng)->max)
					*rng = rnginfo;
			} else
				*rng = rnginfo;
		}
	}

out_get_range:

	if (ret < 0)
		*rng = NULL;

	return ret;
}

/**
 * @brief Convert samples to physical units
 *
 * @param[in] chan Channel descriptor
 * @param[in] rng Range descriptor
 * @param[out] dst Ouput buffer 
 * @param[in] src Input buffer
 * @param[in] cnt Count of conversion to perform
 *
 * @return the count of conversion performed, otherwise a negative
 * error code.
 *
 */
int a4l_to_phys(a4l_chinfo_t * chan,
		a4l_rnginfo_t * rng, double *dst, void *src, int cnt)
{
	int i = 0, j = 0;
	lsampl_t tmp;

	/* Temporary values used for conversion
	   (phys = a * src + b) */
	double a, b;
	/* Temporary data accessor */
	lsampl_t(*datax_get) (void *);

	/* Basic checking */
	if (rng == NULL || chan == NULL)
		return 0;

	/* This converting function only works 
	   if acquired data width is 8, 16 or 32 */
	switch (chan->nb_bits) {
	case 32:
		datax_get = data32_get;
		break;
	case 16:
		datax_get = data16_get;
		break;
	case 8:
		datax_get = data8_get;
		break;
	default:
		return -EINVAL;
	};

	/* Computes the translation factor and the constant only once */
	a = ((double)(rng->max - rng->min)) /
		(((1ULL << chan->nb_bits) - 1) * A4L_RNG_FACTOR);
	b = (double)rng->min / A4L_RNG_FACTOR;

	while (i < cnt) {

		/* Properly retrieves the data */
		tmp = datax_get(src + i);

		/* Performs the conversion */
		dst[j] = a * tmp + b;

		/* Updates the counters */
		i += chan->nb_bits / 8;
		j++;
	}

	return j;
}

/**
 * @brief Convert physical units to samples
 *
 * @param[in] chan Channel descriptor
 * @param[in] rng Range descriptor
 * @param[out] dst Ouput buffer 
 * @param[in] src Input buffer
 * @param[in] cnt Count of conversion to perform
 *
 * @return the count of conversion performed, otherwise a negative
 * error code.
 *
 */
int a4l_from_phys(a4l_chinfo_t * chan,
		  a4l_rnginfo_t * rng, void *dst, double *src, int cnt)
{
	int i = 0, j = 0;

	/* Temporary values used for conversion
	   (dst = a * phys - b) */
	double a, b;
	/* Temporary data accessor */
	void (*datax_set) (void *, lsampl_t);

	/* Basic checking */
	if (rng == NULL || chan == NULL)
		return 0;

	/* This converting function only works 
	   if acquired data width is 8, 16 or 32 */
	switch (chan->nb_bits) {
	case 32:
		datax_set = data32_set;
		break;
	case 16:
		datax_set = data16_set;
		break;
	case 8:
		datax_set = data8_set;
		break;
	default:
		return -EINVAL;
	};

	/* Computes the translation factor and the constant only once */
	a = (((double)A4L_RNG_FACTOR) / (rng->max - rng->min)) *
		((1ULL << chan->nb_bits) - 1);
	b = ((double)(rng->min) / (rng->max - rng->min)) *
		((1ULL << chan->nb_bits) - 1);

	while (i < cnt) {

		/* Performs the conversion */
		datax_set(dst + i, (lsampl_t) (a * src[j] - b));

		/* Updates the counters */
		i += chan->nb_bits / 8;
		j++;
	}

	return j;
}

/** @} Range / conversion  API */

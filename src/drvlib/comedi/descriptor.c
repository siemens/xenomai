/**
 * @file
 * Comedilib for RTDM, descriptor related features  
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

#include <stdio.h>
#include <string.h>

#include <comedi/descriptor.h>
#include <comedi/comedi.h>

#include "syscall.h"
#include "root_leaf.h"

#ifndef DOXYGEN_CPP

static void comedi_root_setup(comedi_root_t * rt,
			      unsigned long gsize, unsigned long rsize)
{
	/* Common init */
	rt->offset = ((void *)rt + sizeof(comedi_root_t));
	rt->gsize = gsize;
	rt->id = 0xffffffff;
	rt->lfnxt = NULL;
	rt->lfchd = NULL;

	/* Specific init */
	rt->data = rt->offset;
	rt->offset += rsize;
}

static int comedi_leaf_add(comedi_root_t * rt,
			   comedi_leaf_t * lf,
			   comedi_leaf_t ** lfchild, unsigned long lfsize)
{
	/* Basic checking */
	if (rt->offset + lfsize > ((void *)rt) + rt->gsize)
		return -ENOMEM;

	if (lf->nb_leaf != 0) {
		int i;
		comedi_leaf_t *lflst = lf->lfchd;

		for (i = 0; i < (lf->nb_leaf - 1); i++) {
			if (lflst == NULL)
				return -EFAULT;
			else
				lflst = lflst->lfnxt;
		}
		lflst->lfnxt = (comedi_leaf_t *) rt->offset;
	} else
		lf->lfchd = (comedi_leaf_t *) rt->offset;

	/* Inits parent leaf */
	lf->nb_leaf++;
	*lfchild = (comedi_leaf_t *) rt->offset;
	rt->offset += sizeof(comedi_leaf_t);

	/* Performs child leaf init */
	(*lfchild)->id = lf->nb_leaf - 1;
	(*lfchild)->nb_leaf = 0;
	(*lfchild)->lfnxt = NULL;
	(*lfchild)->lfchd = NULL;
	(*lfchild)->data = (void *)rt->offset;

	/* Performs root modifications */
	rt->offset += lfsize;

	return 0;
}

static inline comedi_leaf_t *comedi_leaf_get(comedi_leaf_t * lf,
					     unsigned int id)
{
	int i;
	comedi_leaf_t *lflst = lf->lfchd;

	for (i = 0; i < id && lflst != NULL; i++)
		lflst = lflst->lfnxt;

	return lflst;
}

static int __comedi_get_sbsize(int fd, comedi_desc_t * dsc)
{
	unsigned int i, j, nb_chan, nb_rng;
	int ret, res =
	    dsc->nb_subd * (sizeof(comedi_sbinfo_t) + sizeof(comedi_leaf_t));

	for (i = 0; i < dsc->nb_subd; i++) {
		if ((ret = comedi_sys_nbchaninfo(fd, i, &nb_chan)) < 0)
			return ret;
		res +=
		    nb_chan * (sizeof(comedi_chinfo_t) + sizeof(comedi_leaf_t));
		for (j = 0; j < nb_chan; j++) {
			if ((ret = comedi_sys_nbrnginfo(fd, i, j, &nb_rng)) < 0)
				return ret;
			res +=
			    nb_rng * (sizeof(comedi_rnginfo_t) +
				      sizeof(comedi_leaf_t));
		}
	}

	return res;
}

static int __comedi_fill_desc(int fd, comedi_desc_t * dsc)
{
	int ret;
	unsigned int i, j;
	comedi_sbinfo_t *sbinfo;
	comedi_root_t *rt = (comedi_root_t *) dsc->sbdata;

	comedi_root_setup(rt, dsc->sbsize,
			  dsc->nb_subd * sizeof(comedi_sbinfo_t));
	sbinfo = (comedi_sbinfo_t *) rt->data;

	if ((ret = comedi_sys_subdinfo(fd, sbinfo)) < 0)
		return ret;

	for (i = 0; i < dsc->nb_subd; i++) {
		comedi_leaf_t *lfs;
		comedi_chinfo_t *chinfo;
		comedi_leaf_add(rt, (comedi_leaf_t *) rt, &lfs,
				sbinfo[i].nb_chan * sizeof(comedi_chinfo_t));

		chinfo = (comedi_chinfo_t *) lfs->data;

		if ((ret = comedi_sys_chaninfo(fd, i, chinfo)) < 0)
			return ret;
		for (j = 0; j < sbinfo[i].nb_chan; j++) {
			comedi_leaf_t *lfc;
			comedi_rnginfo_t *rnginfo;
			comedi_leaf_add(rt, lfs, &lfc,
					chinfo[j].nb_rng *
					sizeof(comedi_rnginfo_t));

			rnginfo = (comedi_rnginfo_t *) lfc->data;
			if ((ret = comedi_sys_rnginfo(fd, i, j, rnginfo)) < 0)
				return ret;
		}
	}

	return 0;
}

#endif /* !DOXYGEN_CPP */

/*!
 * @ingroup syscall
 * @defgroup descriptor_sys Descriptor Syscall API
 * @{
 */

/**
 * @brief Get a Comedilib descriptor on an attached device
 *
 * Once the device has been attached, the function comedi_get_desc()
 * retrieves various information on the device (subdevices, channels,
 * ranges, etc.).
 * The function comedi_get_desc() can be called twice:
 * - The first time, almost all the fields, except sbdata, are set
 *   (board_name, nb_subd, idx_read_subd, idx_write_subd, magic,
 *   sbsize); the last field , sbdata, is supposed to be a pointer on
 *   a buffer, which size is defined by the field sbsize.
 * - The second time, the buffer pointed by sbdata is filled with data
 *   about the subdevices, the channels and the ranges.
 * 
 * Between the two calls, an allocation must be performed in order to
 * recover a buffer large enough to contain all the data. These data
 * are set up according a root-leaf organization (device -> subdevice
 * -> channel -> range). They cannot be accessed directly; specific
 * functions are available so as to retrieve them:
 * - comedi_get_subdinfo() to get some subdevice's characteristics.
 * - comedi_get_chaninfo() to get some channel's characteristics.
 * - comedi_get_rnginfo() to get some range's characteristics.
 *
 * @param[in] fd Driver file descriptor
 * @param[out] dsc Device descriptor
 * @param[in] pass Description level to retrieve:
 * - COMEDI_BSC_DESC to get the basic descriptor (notably the size of
 *   the data buffer to allocate).
 * - COMEDI_CPLX_DESC to get the complex descriptor, the data buffer
 *   is filled with characteristics about the subdevices, the channels
 *   and the ranges.
 *
 * @return 0 on success, otherwise negative error code.
 *
 */
int comedi_sys_desc(int fd, comedi_desc_t * dsc, int pass)
{
	int ret = 0;
	if (dsc == NULL ||
	    (pass != COMEDI_BSC_DESC && dsc->magic != MAGIC_BSC_DESC))
		return -EINVAL;

	if (pass == COMEDI_BSC_DESC) {

		ret = comedi_sys_devinfo(fd, (comedi_dvinfo_t *) dsc);
		if (ret < 0)
			goto out_comedi_sys_desc;

		dsc->sbsize = __comedi_get_sbsize(fd, dsc);
		dsc->sbdata = NULL;
		dsc->magic = MAGIC_BSC_DESC;
	} else {

		ret = __comedi_fill_desc(fd, dsc);
		if (ret < 0)
			goto out_comedi_sys_desc;

		dsc->magic = MAGIC_CPLX_DESC;
	}

      out_comedi_sys_desc:
	return ret;
}

/*! @} Descriptor Syscall API */

/*!
 * @ingroup level1_lib
 * @defgroup descriptor1_lib Descriptor API
 *
 * This is the API interface used to fill and use Comedilib device
 * descriptor structure
 * @{
 */

/**
 * @brief Open a Comedi device and basically fill the descriptor
 *
 * @param[out] dsc Device descriptor
 * @param[in] fname Device name
 *
 * @return 0 on success, otherwise negative error code.
 *
 */
int comedi_open(comedi_desc_t * dsc, const char *fname)
{
	int ret;

	/* Basic checking */
	if (dsc == NULL)
		return -EINVAL;

	/* Initializes the descriptor */
	memset(dsc, 0, sizeof(comedi_desc_t));

	/* Opens the driver */
	dsc->fd = comedi_sys_open(fname);
	if (dsc->fd < 0)
		return dsc->fd;

	/* Basically fills the descriptor */
	ret = comedi_sys_desc(dsc->fd, dsc, COMEDI_BSC_DESC);
	if (ret < 0) {
		comedi_sys_close(dsc->fd);
	}

	return ret;
}

/**
 * @brief Close the Comedi device related with the descriptor
 *
 * @param[in] dsc Device descriptor
 *
 * @return 0 on success, otherwise negative error code.
 *
 */
int comedi_close(comedi_desc_t * dsc)
{
	/* Basic checking */
	if (dsc == NULL)
		return -EINVAL;

	return comedi_sys_close(dsc->fd);
}

/**
 * @brief Fill the descriptor with subdevices, channels and ranges
 * data
 *
 * @param[in] dsc Device descriptor partly filled by comedi_open().
 *
 * @return 0 on success, otherwise negative error code.
 *
 */
int comedi_fill_desc(comedi_desc_t * dsc)
{
	/* Basic checking */
	if (dsc == NULL || dsc->fd < 0)
		return -EINVAL;

	/* Checks the descriptor has been basically filled */
	if (dsc->magic != MAGIC_BSC_DESC)
		return -EINVAL;

	return comedi_sys_desc(dsc->fd, dsc, COMEDI_CPLX_DESC);
}

/**
 * @brief Get an information structure on a specified subdevice
 *
 * @param[in] dsc Device descriptor filled by comedi_open() and
 * comedi_fill_desc()
 * @param[in] subd Subdevice index
 * @param[out] info Subdevice information structure
 *
 * @return 0 on success, otherwise negative error code.
 *
 */
int comedi_get_subdinfo(comedi_desc_t * dsc,
			unsigned int subd, comedi_sbinfo_t ** info)
{
	comedi_leaf_t *tmp;

	if (dsc == NULL || info == NULL)
		return -EINVAL;

	if (dsc->magic != MAGIC_CPLX_DESC)
		return -EINVAL;

	if (subd >= dsc->nb_subd)
		return -EINVAL;

	tmp = (comedi_leaf_t *) dsc->sbdata;
	*info = &(((comedi_sbinfo_t *) tmp->data)[subd]);

	return 0;
}

/**
 * @brief Get an information structure on a specified channel
 *
 * @param[in] dsc Device descriptor filled by comedi_open() and
 * comedi_fill_desc()
 * @param[in] subd Subdevice index
 * @param[in] chan Channel index
 * @param[out] info Channel information structure
 *
 * @return 0 on success, otherwise negative error code.
 *
 */
int comedi_get_chinfo(comedi_desc_t * dsc,
		      unsigned int subd,
		      unsigned int chan, comedi_chinfo_t ** info)
{
	comedi_leaf_t *tmp;

	if (dsc == NULL || info == NULL)
		return -EINVAL;

	if (dsc->magic != MAGIC_CPLX_DESC)
		return -EINVAL;

	if (subd >= dsc->nb_subd)
		return -EINVAL;

	tmp = (comedi_leaf_t *) dsc->sbdata;

	if (chan >= ((comedi_sbinfo_t *) tmp->data)[subd].nb_chan)
		return -EINVAL;

	tmp = comedi_leaf_get(tmp, subd);
	*info = &(((comedi_chinfo_t *) tmp->data)[chan]);

	return 0;
}

/**
 * @brief Get an information structure on a specified range
 *
 * @param[in] dsc Device descriptor filled by comedi_open() and
 * comedi_fill_desc()
 * @param[in] subd Subdevice index
 * @param[in] chan Channel index
 * @param[in] rng Range index
 * @param[out] info Range information structure
 *
 * @return 0 on success, otherwise negative error code.
 *
 */
int comedi_get_rnginfo(comedi_desc_t * dsc,
		       unsigned int subd,
		       unsigned int chan,
		       unsigned int rng, comedi_rnginfo_t ** info)
{
	comedi_leaf_t *tmp;

	if (dsc == NULL || info == NULL)
		return -EINVAL;

	if (dsc->magic != MAGIC_CPLX_DESC)
		return -EINVAL;

	if (subd >= dsc->nb_subd)
		return -EINVAL;

	tmp = (comedi_leaf_t *) dsc->sbdata;

	if (chan >= ((comedi_sbinfo_t *) tmp->data)[subd].nb_chan)
		return -EINVAL;

	tmp = comedi_leaf_get(tmp, subd);

	if (rng >= ((comedi_chinfo_t *) tmp->data)[chan].nb_rng)
		return -EINVAL;

	tmp = comedi_leaf_get(tmp, chan);
	*info = &(((comedi_rnginfo_t *) tmp->data)[rng]);

	return 0;
}

/*! @} Descriptor API */

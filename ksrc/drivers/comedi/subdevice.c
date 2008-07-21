/**
 * @file
 * Comedi for RTDM, subdevice, channel and range related features
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

#ifndef DOXYGEN_CPP

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/mman.h>
#include <asm/io.h>
#include <asm/errno.h>

#include <comedi/context.h>
#include <comedi/subdevice.h>
#include <comedi/device.h>
#include <comedi/channel_range.h>

/* --- Common ranges declarations --- */

comedi_rngtab_t rng_bipolar10 = { 1, {
				      RANGE_V(-10, 10),
				      }
};
comedi_rngdesc_t range_bipolar10 = RNG_GLOBAL(rng_bipolar10);

comedi_rngtab_t rng_bipolar5 = { 1, {
				     RANGE_V(-5, 5),
				     }
};
comedi_rngdesc_t range_bipolar5 = RNG_GLOBAL(rng_bipolar5);

comedi_rngtab_t rng_unipolar10 = { 1, {
				       RANGE_V(0, 10),
				       }
};
comedi_rngdesc_t range_unipolar10 = RNG_GLOBAL(rng_unipolar10);

comedi_rngtab_t rng_unipolar5 = { 1, {
				      RANGE_V(0, 5),
				      }
};
comedi_rngdesc_t range_unipolar5 = RNG_GLOBAL(rng_unipolar10);

/* --- Basic channel / range management functions --- */

comedi_chan_t *comedi_get_chfeat(comedi_subd_t * sb, int idx)
{
	int i = (sb->chan_desc->mode != COMEDI_CHAN_GLOBAL_CHANDESC) ? idx : 0;
	return &(sb->chan_desc->chans[i]);
}

comedi_rng_t *comedi_get_rngfeat(comedi_subd_t * sb, int chidx, int rngidx)
{
	int i = (sb->rng_desc->mode != COMEDI_RNG_GLOBAL_RNGDESC) ? chidx : 0;
	return &(sb->rng_desc->rngtabs[i]->rngs[rngidx]);
}

int comedi_check_chanlist(comedi_subd_t * subd,
			  unsigned char nb_chan, unsigned int *chans)
{
	int i;

	if (nb_chan > subd->chan_desc->length)
		return -EINVAL;

	for (i = 0; i < nb_chan; i++) {
		int j =
		    (subd->rng_desc->mode != COMEDI_RNG_GLOBAL_RNGDESC) ? i : 0;
		int k =
		    (subd->chan_desc->mode !=
		     COMEDI_CHAN_GLOBAL_CHANDESC) ? i : 0;

		if (CR_CHAN(chans[i]) >= subd->chan_desc->length) {
			comedi_logerr
			    ("comedi_check_chanlist: chan idx out_of range (%u>=%u)\n",
			     CR_CHAN(chans[i]), subd->chan_desc->length);
			return -EINVAL;
		}
		if (CR_RNG(chans[i]) > subd->rng_desc->rngtabs[j]->length) {
			comedi_logerr
			    ("comedi_check_chanlist: rng idx out_of range (%u>=%u)\n",
			     CR_RNG(chans[i]),
			     subd->rng_desc->rngtabs[j]->length);
			return -EINVAL;
		}
		if (CR_AREF(chans[i]) != 0 &&
		    (CR_AREF(chans[i]) & subd->chan_desc->chans[k].flags) == 0)
		{
			comedi_logerr
			    ("comedi_check_chanlist: bad channel type\n");
			return -EINVAL;
		}
	}

	return 0;
}

/* --- Upper layer functions --- */

int comedi_get_nbchan(comedi_dev_t * dev, int subd_key)
{
	return dev->transfer->subds[subd_key]->chan_desc->length;
}

int comedi_add_subd(comedi_drv_t * drv, comedi_subd_t * subd)
{
	struct list_head *this;
	comedi_subd_t *news;
	int i = 0;

	/* Basic checking */
	if (drv == NULL || subd == NULL)
		return -EINVAL;

	/* The driver developer does not have to manage instances
	   of the subdevice structure; the allocation are done
	   in the Comedi layer */
	news = comedi_kmalloc(sizeof(comedi_subd_t));
	if (news == NULL)
		return -ENOMEM;
	memcpy(news, subd, sizeof(comedi_subd_t));

	list_add_tail(&news->list, &drv->subdvsq);

	list_for_each(this, &drv->subdvsq) {
		i++;
	}

	return --i;
}

/* --- IOCTL / FOPS functions --- */

int comedi_ioctl_subdinfo(comedi_cxt_t * cxt, void *arg)
{
	comedi_dev_t *dev = comedi_get_dev(cxt);
	int i, ret = 0;
	comedi_sbinfo_t *subd_info;

	/* Basic checking */
	if (!test_bit(COMEDI_DEV_ATTACHED, &dev->flags))
		return -EINVAL;

	subd_info =
	    comedi_kmalloc(dev->transfer->nb_subd * sizeof(comedi_sbinfo_t));
	if (subd_info == NULL)
		return -ENOMEM;

	for (i = 0; i < dev->transfer->nb_subd; i++) {
		subd_info[i].flags = dev->transfer->subds[i]->flags;
		subd_info[i].status = dev->transfer->status[i];
		subd_info[i].nb_chan =
		    dev->transfer->subds[i]->chan_desc->length;
	}

	if (comedi_copy_to_user(cxt,
				arg,
				subd_info, dev->transfer->nb_subd *
				sizeof(comedi_sbinfo_t)) != 0)
		ret = -EFAULT;

	comedi_kfree(subd_info);

	return ret;

}

int comedi_ioctl_nbchaninfo(comedi_cxt_t * cxt, void *arg)
{
	comedi_dev_t *dev = comedi_get_dev(cxt);
	comedi_chinfo_arg_t inarg;

	/* Basic checking */
	if (!dev->flags & COMEDI_DEV_ATTACHED)
		return -EINVAL;

	if (comedi_copy_from_user(cxt,
				  &inarg, arg,
				  sizeof(comedi_chinfo_arg_t)) != 0)
		return -EFAULT;

	if (inarg.idx_subd >= dev->transfer->nb_subd)
		return -EINVAL;

	inarg.info =
	    (void *)(unsigned long)dev->transfer->subds[inarg.idx_subd]->
	    chan_desc->length;

	if (comedi_copy_to_user(cxt,
				arg, &inarg, sizeof(comedi_chinfo_arg_t)) != 0)
		return -EFAULT;

	return 0;
}

int comedi_ioctl_chaninfo(comedi_cxt_t * cxt, void *arg)
{
	int i, ret = 0;
	comedi_dev_t *dev = comedi_get_dev(cxt);
	comedi_chinfo_t *chan_info;
	comedi_chinfo_arg_t inarg;
	comedi_chdesc_t *chan_desc;
	comedi_rngdesc_t *rng_desc;

	/* Basic checking */
	if (!test_bit(COMEDI_DEV_ATTACHED, &dev->flags))
		return -EINVAL;

	if (comedi_copy_from_user(cxt,
				  &inarg, arg,
				  sizeof(comedi_chinfo_arg_t)) != 0)
		return -EFAULT;

	if (inarg.idx_subd >= dev->transfer->nb_subd)
		return -EINVAL;

	chan_desc = dev->transfer->subds[inarg.idx_subd]->chan_desc;
	rng_desc = dev->transfer->subds[inarg.idx_subd]->rng_desc;

	chan_info = comedi_kmalloc(chan_desc->length * sizeof(comedi_chinfo_t));
	if (chan_info == NULL)
		return -ENOMEM;

	/* If the channel descriptor is global, the fields are filled 
	   with the same instance of channel descriptor */
	for (i = 0; i < chan_desc->length; i++) {
		int j =
		    (chan_desc->mode != COMEDI_CHAN_GLOBAL_CHANDESC) ? i : 0;
		int k = (rng_desc->mode != COMEDI_RNG_GLOBAL_RNGDESC) ? i : 0;

		chan_info[i].chan_flags = chan_desc->chans[j].flags;
		chan_info[i].nb_bits = chan_desc->chans[j].nb_bits;
		chan_info[i].nb_rng = rng_desc->rngtabs[k]->length;

		if (chan_desc->mode == COMEDI_CHAN_GLOBAL_CHANDESC)
			chan_info[i].chan_flags |= COMEDI_CHAN_GLOBAL;
	}

	if (comedi_copy_to_user(cxt,
				inarg.info,
				chan_info,
				chan_desc->length * sizeof(comedi_chinfo_t)) !=
	    0)
		return -EFAULT;

	comedi_kfree(chan_info);

	return ret;
}

int comedi_ioctl_nbrnginfo(comedi_cxt_t * cxt, void *arg)
{
	int i;
	comedi_dev_t *dev = comedi_get_dev(cxt);
	comedi_rnginfo_arg_t inarg;
	comedi_rngdesc_t *rng_desc;

	/* Basic checking */
	if (!test_bit(COMEDI_DEV_ATTACHED, &dev->flags))
		return -EINVAL;

	if (comedi_copy_from_user(cxt,
				  &inarg,
				  arg, sizeof(comedi_rnginfo_arg_t)) != 0)
		return -EFAULT;

	if (inarg.idx_chan >=
	    dev->transfer->subds[inarg.idx_subd]->chan_desc->length)
		return -EINVAL;

	rng_desc = dev->transfer->subds[inarg.idx_subd]->rng_desc;
	i = (rng_desc->mode != COMEDI_RNG_GLOBAL_RNGDESC) ? inarg.idx_chan : 0;
	inarg.info = (void *)(unsigned long)rng_desc->rngtabs[i]->length;

	if (comedi_copy_to_user(cxt,
				arg, &inarg, sizeof(comedi_rnginfo_arg_t)) != 0)
		return -EFAULT;

	return 0;
}

int comedi_ioctl_rnginfo(comedi_cxt_t * cxt, void *arg)
{
	int i, ret = 0;
	unsigned int tmp;
	comedi_dev_t *dev = comedi_get_dev(cxt);
	comedi_rngdesc_t *rng_desc;
	comedi_rnginfo_t *rng_info;
	comedi_rnginfo_arg_t inarg;

	/* Basic checking */
	if (!test_bit(COMEDI_DEV_ATTACHED, &dev->flags))
		return -EINVAL;

	if (comedi_copy_from_user(cxt,
				  &inarg,
				  arg, sizeof(comedi_rnginfo_arg_t)) != 0)
		return -EFAULT;

	if (inarg.idx_subd >= dev->transfer->nb_subd)
		return -EINVAL;

	if (inarg.idx_chan >=
	    dev->transfer->subds[inarg.idx_subd]->chan_desc->length)
		return -EINVAL;

	/* If the range descriptor is global, 
	   we take the first instance */
	rng_desc = dev->transfer->subds[inarg.idx_subd]->rng_desc;
	tmp = (rng_desc->mode != COMEDI_RNG_GLOBAL_RNGDESC) ?
	    inarg.idx_chan : 0;

	rng_info = comedi_kmalloc(rng_desc->rngtabs[tmp]->length *
				  sizeof(comedi_rnginfo_t));
	if (rng_info == NULL)
		return -ENOMEM;

	for (i = 0; i < rng_desc->rngtabs[tmp]->length; i++) {
		rng_info[i].min = rng_desc->rngtabs[tmp]->rngs[i].min;
		rng_info[i].max = rng_desc->rngtabs[tmp]->rngs[i].max;
		rng_info[i].flags = rng_desc->rngtabs[tmp]->rngs[i].flags;

		if (rng_desc->mode == COMEDI_RNG_GLOBAL_RNGDESC)
			rng_info[i].flags |= COMEDI_RNG_GLOBAL;
	}

	if (comedi_copy_to_user(cxt,
				inarg.info,
				rng_info,
				rng_desc->rngtabs[tmp]->length *
				sizeof(comedi_rnginfo_t)) != 0)
		return -EFAULT;

	comedi_kfree(rng_info);

	return ret;
}

#endif /* !DOXYGEN_CPP */

/**
 * @file
 * Analogy for Linux, instruction related features
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
#include <linux/version.h>
#include <linux/ioport.h>
#include <linux/mman.h>
#include <asm/div64.h>
#include <asm/io.h>
#include <asm/errno.h>

#include <analogy/context.h>
#include <analogy/device.h>

int a4l_do_insn_gettime(a4l_kinsn_t * dsc)
{
	unsigned long long ns;
	unsigned long ns2;

	/* Basic checkings */
	if (dsc->data_size != 2)
		return -EINVAL;

	ns = a4l_get_time();

	ns2 = do_div(ns, 1000000000);
	dsc->data[0] = (lsampl_t) ns;
	dsc->data[1] = (lsampl_t) ns2 / 1000;

	return 0;
}

int a4l_do_insn_wait(a4l_kinsn_t * dsc)
{
	unsigned int us;

	/* Basic checkings */
	if (dsc->data_size != 1)
		return -EINVAL;

	if (dsc->data[0] > A4L_INSN_WAIT_MAX)
		return -EINVAL;

	/* As we use (a4l_)udelay, we have to convert the delay into
	   microseconds */
	us = dsc->data[0] / 1000;

	/* At least, the delay is rounded up to 1 microsecond */
	if (us == 0)
		us = 1;

	/* Performs the busy waiting */
	a4l_udelay(us);

	return 0;
}

int a4l_do_insn_trig(a4l_cxt_t * cxt, a4l_kinsn_t * dsc)
{
	a4l_subd_t *subd;
	a4l_dev_t *dev = a4l_get_dev(cxt);
	lsampl_t trignum;

	/* Basic checkings */
	if (dsc->data_size > 1)
		return -EINVAL;
	
	trignum = (dsc->data_size == 1) ? dsc->data[0] : 0;
	
	if (dsc->idx_subd >= dev->transfer.nb_subd)
		return -EINVAL;

	subd = dev->transfer.subds[dsc->idx_subd];

	/* Checks that the concerned subdevice is trigger-compliant */
	if ((subd->flags & A4L_SUBD_CMD) == 0 || subd->trigger == NULL)
		return -EINVAL;

	/* Performs the trigger */
	return subd->trigger(subd, trignum);
}

int a4l_fill_insndsc(a4l_cxt_t * cxt, a4l_kinsn_t * dsc, void *arg)
{
	int ret = 0;
	void *tmp_data = NULL;

	ret = rtdm_safe_copy_from_user(cxt->user_info, 
				       dsc, arg, sizeof(a4l_insn_t));
	if (ret != 0)
		goto out_insndsc;

	if (dsc->data_size != 0 && dsc->data == NULL) {
		ret = -EINVAL;
		goto out_insndsc;
	}

	if (dsc->data_size != 0 && dsc->data != NULL) {
		tmp_data = rtdm_malloc(dsc->data_size);
		if (tmp_data == NULL) {
			ret = -ENOMEM;
			goto out_insndsc;
		}

		if ((dsc->type & A4L_INSN_MASK_WRITE) != 0) {
			ret = rtdm_safe_copy_from_user(cxt->user_info,
						       tmp_data, dsc->data,
						       dsc->data_size);
			if (ret < 0)
				goto out_insndsc;
		}
	}

	dsc->__udata = dsc->data;
	dsc->data = tmp_data;

out_insndsc:

	if (ret != 0 && tmp_data != NULL)
		rtdm_free(tmp_data);

	return ret;
}

int a4l_free_insndsc(a4l_cxt_t * cxt, a4l_kinsn_t * dsc)
{
	int ret = 0;

	if ((dsc->type & A4L_INSN_MASK_READ) != 0)
		ret = rtdm_safe_copy_to_user(cxt->user_info,
					     dsc->__udata,
					     dsc->data, dsc->data_size);

	if (dsc->data != NULL)
		rtdm_free(dsc->data);

	return ret;
}

int a4l_do_special_insn(a4l_cxt_t * cxt, a4l_kinsn_t * dsc)
{
	int ret = 0;

	switch (dsc->type) {
	case A4L_INSN_GTOD:
		ret = a4l_do_insn_gettime(dsc);
		break;
	case A4L_INSN_WAIT:
		ret = a4l_do_insn_wait(dsc);
		break;
	case A4L_INSN_INTTRIG:
		ret = a4l_do_insn_trig(cxt, dsc);
		break;
	default:
		__a4l_err("a4l_do_special_insn: "
			  "incoherent instruction code\n");
		ret = -EINVAL;
	}

	return ret;
}

int a4l_do_insn(a4l_cxt_t * cxt, a4l_kinsn_t * dsc)
{
	int ret;
	a4l_subd_t *subd;
	a4l_dev_t *dev = a4l_get_dev(cxt);

	/* Checks the subdevice index */
	if (dsc->idx_subd >= dev->transfer.nb_subd) {
		__a4l_err("a4l_do_insn: bad subdevice index\n");
		return -EINVAL;
	}

	/* Recovers pointers on the proper subdevice */
	subd = dev->transfer.subds[dsc->idx_subd];

	/* Checks the subdevice's characteristics */
	if (((subd->flags & A4L_SUBD_UNUSED) != 0) ||
	    ((subd->flags & A4L_SUBD_CMD) == 0)) {
		__a4l_err("a4l_do_insn: wrong subdevice selected\n");
		return -EINVAL;
	}

	/* Checks the channel descriptor */
	ret = a4l_check_chanlist(dev->transfer.subds[dsc->idx_subd],
				 1, &dsc->chan_desc);
	if (ret < 0)
		return ret;

	/* Prevents the subdevice from being used during 
	   the following operations */
	ret = a4l_reserve_transfer(cxt, dsc->idx_subd);
	if (ret < 0)
		goto out_do_insn;

	/* Lets the driver-specific code perform the instruction */
	switch (dsc->type) {
	case A4L_INSN_READ:
		ret = subd->insn_read(subd, dsc);
		break;
	case A4L_INSN_WRITE:
		ret = subd->insn_write(subd, dsc);
		break;
	case A4L_INSN_BITS:
		ret = subd->insn_bits(subd, dsc);
		break;
	case A4L_INSN_CONFIG:
		ret = subd->insn_config(subd, dsc);
		break;
	default:
		ret = -EINVAL;
	}

out_do_insn:

	/* Releases the subdevice from its reserved state */
	a4l_cancel_transfer(cxt, dsc->idx_subd);

	return ret;
}

int a4l_ioctl_insn(a4l_cxt_t * cxt, void *arg)
{
	int ret = 0;
	a4l_kinsn_t insn;
	a4l_dev_t *dev = a4l_get_dev(cxt);

	/* Basic checking */
	if (!test_bit(A4L_DEV_ATTACHED, &dev->flags)) {
		__a4l_err("a4l_ioctl_insn: unattached device\n");
		return -EINVAL;
	}

	/* Recovers the instruction descriptor */
	ret = a4l_fill_insndsc(cxt, &insn, arg);
	if (ret != 0)
		goto err_ioctl_insn;

	/* Performs the instruction */
	if ((insn.type & A4L_INSN_MASK_SPECIAL) != 0)
		ret = a4l_do_special_insn(cxt, &insn);
	else
		ret = a4l_do_insn(cxt, &insn);

	if (ret < 0)
		goto err_ioctl_insn;

	/* Frees the used memory and sends back some
	   data, if need be */
	ret = a4l_free_insndsc(cxt, &insn);

	return ret;

err_ioctl_insn:
	a4l_free_insndsc(cxt, &insn);
	return ret;
}

int a4l_fill_ilstdsc(a4l_cxt_t * cxt, a4l_kilst_t * dsc, void *arg)
{
	int i, ret = 0;

	dsc->insns = NULL;

	/* Recovers the structure from user space */
	ret = rtdm_safe_copy_from_user(cxt->user_info, 
				       dsc, arg, sizeof(a4l_insnlst_t));
	if (ret < 0)
		return ret;

	/* Some basic checking */
	if (dsc->count == 0)
		return -EINVAL;

	/* Keeps the user pointer in an opaque field */
	dsc->__uinsns = dsc->insns;

	dsc->insns = rtdm_malloc(dsc->count * sizeof(a4l_kinsn_t));
	if (dsc->insns == NULL)
		return -ENOMEM;

	/* Recovers the instructions, one by one. This part is not 
	   optimized */
	for (i = 0; i < dsc->count && ret == 0; i++)
		ret = a4l_fill_insndsc(cxt,
				       &(dsc->insns[i]),
				       &(dsc->__uinsns[i]));

	/* In case of error, frees the allocated memory */
	if (ret < 0 && dsc->insns != NULL)
		rtdm_free(dsc->insns);

	return ret;
}

int a4l_free_ilstdsc(a4l_cxt_t * cxt, a4l_kilst_t * dsc)
{
	int i, ret = 0;

	if (dsc->insns != NULL) {

		for (i = 0; i < dsc->count && ret == 0; i++)
			ret = a4l_free_insndsc(cxt, &(dsc->insns[i]));

		while (i < dsc->count) {
			a4l_free_insndsc(cxt, &(dsc->insns[i]));
			i++;
		}

		rtdm_free(dsc->insns);
	}

	return ret;
}

/* This function is not optimized in terms of memory footprint and
   CPU charge; however, the whole analogy instruction system was not
   designed for performance issues */
int a4l_ioctl_insnlist(a4l_cxt_t * cxt, void *arg)
{
	int i, ret = 0;
	a4l_kilst_t ilst;
	a4l_dev_t *dev = a4l_get_dev(cxt);

	/* Basic checking */
	if (!test_bit(A4L_DEV_ATTACHED, &dev->flags)) {
		__a4l_err("a4l_ioctl_insnlist: unattached device\n");
		return -EINVAL;
	}

	if ((ret = a4l_fill_ilstdsc(cxt, &ilst, arg)) < 0)
		return ret;

	/* Performs the instructions */
	for (i = 0; i < ilst.count && ret == 0; i++) {
		if ((ilst.insns[i].type & A4L_INSN_MASK_SPECIAL) != 0)
			ret = a4l_do_special_insn(cxt, &ilst.insns[i]);
		else
			ret = a4l_do_insn(cxt, &ilst.insns[i]);
	}

	if (ret < 0)
		goto err_ioctl_ilst;

	return a4l_free_ilstdsc(cxt, &ilst);

err_ioctl_ilst:
	a4l_free_ilstdsc(cxt, &ilst);
	return ret;
}

#endif /* !DOXYGEN_CPP */

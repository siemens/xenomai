/**
 * @file
 * Comedi for RTDM, command related features
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
#include <comedi/device.h>

/* --- Command descriptor management functions --- */

int comedi_fill_cmddesc(comedi_cxt_t * cxt, comedi_cmd_t * desc, void *arg)
{
	int ret = 0;
	unsigned int *tmpchans = NULL;

	ret = comedi_copy_from_user(cxt, desc, arg, sizeof(comedi_cmd_t));
	if (ret != 0)
		goto out_cmddesc;

	if (desc->nb_chan == 0) {
		ret = -EINVAL;
		goto out_cmddesc;
	}

	tmpchans = comedi_kmalloc(desc->nb_chan * sizeof(unsigned int));
	if (tmpchans == NULL) {
		ret = -ENOMEM;
		goto out_cmddesc;
	}

	ret = comedi_copy_from_user(cxt,
				    tmpchans,
				    desc->chan_descs,
				    desc->nb_chan * sizeof(unsigned long));
	if (ret != 0)
		goto out_cmddesc;

	desc->chan_descs = tmpchans;

	comedi_loginfo("comedi_fill_cmddesc: desc dump\n");
	comedi_loginfo("\t->idx_subd=%u\n", desc->idx_subd);
	comedi_loginfo("\t->flags=%lu\n", desc->flags);
	comedi_loginfo("\t->nb_chan=%u\n", desc->nb_chan);
	comedi_loginfo("\t->chan_descs=0x%x\n", *desc->chan_descs);
	comedi_loginfo("\t->data_len=%u\n", desc->data_len);
	comedi_loginfo("\t->pdata=0x%p\n", desc->data);

      out_cmddesc:

	if (ret != 0) {
		if (tmpchans != NULL)
			comedi_kfree(tmpchans);
		desc->chan_descs = NULL;
	}

	return ret;
}

void comedi_free_cmddesc(comedi_cmd_t * desc)
{
	if (desc->chan_descs != NULL)
		comedi_kfree(desc->chan_descs);
}

int comedi_check_cmddesc(comedi_cxt_t * cxt, comedi_cmd_t * desc)
{
	int ret = 0;
	comedi_dev_t *dev = comedi_get_dev(cxt);

	comedi_loginfo("comedi_check_cmddesc: minor=%d\n",
		       comedi_get_minor(cxt));

	if (desc->idx_subd >= dev->transfer->nb_subd) {
		comedi_logerr
		    ("comedi_check_cmddesc: subdevice index out of range (%u >= %u)\n",
		     desc->idx_subd, dev->transfer->nb_subd);
		return -EINVAL;
	}

	if (dev->transfer->subds[desc->idx_subd]->flags & COMEDI_SUBD_UNUSED) {
		comedi_logerr
		    ("comedi_check_cmddesc: subdevice type incoherent\n");
		return -EIO;
	}

	if (!(dev->transfer->subds[desc->idx_subd]->flags & COMEDI_SUBD_CMD)) {
		comedi_logerr
		    ("comedi_check_cmddesc: operation not supported\n");
		return -EIO;
	}

	if (test_bit(COMEDI_TSF_BUSY, &(dev->transfer->status[desc->idx_subd])))
		return -EBUSY;

	if (ret != 0) {
		comedi_logerr("comedi_check_cmddesc: subdevice busy\n");
		return ret;
	}

	return comedi_check_chanlist(dev->transfer->subds[desc->idx_subd],
				     desc->nb_chan, desc->chan_descs);
}

/* --- Command checking functions --- */

int comedi_check_generic_cmdcnt(comedi_cmd_t * desc)
{
	unsigned int tmp1, tmp2;

	/* Makes sure trigger sources are trivially valid */
	tmp1 =
	    desc->start_src & ~(TRIG_NOW | TRIG_INT | TRIG_EXT | TRIG_FOLLOW);
	tmp2 = desc->start_src & (TRIG_NOW | TRIG_INT | TRIG_EXT | TRIG_FOLLOW);
	if (tmp1 != 0 || tmp2 == 0)
		return -EINVAL;

	tmp1 = desc->scan_begin_src & ~(TRIG_TIMER | TRIG_EXT | TRIG_FOLLOW);
	tmp2 = desc->scan_begin_src & (TRIG_TIMER | TRIG_EXT | TRIG_FOLLOW);
	if (tmp1 != 0 || tmp2 == 0)
		return -EINVAL;

	tmp1 = desc->convert_src & ~(TRIG_TIMER | TRIG_EXT | TRIG_NOW);
	tmp2 = desc->convert_src & (TRIG_TIMER | TRIG_EXT | TRIG_NOW);
	if (tmp1 != 0 || tmp2 == 0)
		return -EINVAL;

	tmp1 = desc->scan_end_src & ~(TRIG_COUNT);
	if (tmp1 != 0)
		return -EINVAL;

	tmp1 = desc->stop_src & ~(TRIG_COUNT | TRIG_NONE);
	tmp2 = desc->stop_src & (TRIG_COUNT | TRIG_NONE);
	if (tmp1 != 0 || tmp2 == 0)
		return -EINVAL;

	/* Makes sure trigger sources are unique */
	if (desc->start_src != TRIG_NOW &&
	    desc->start_src != TRIG_INT &&
	    desc->start_src != TRIG_EXT && desc->start_src != TRIG_FOLLOW)
		return -EINVAL;

	if (desc->scan_begin_src != TRIG_TIMER &&
	    desc->scan_begin_src != TRIG_EXT &&
	    desc->scan_begin_src != TRIG_FOLLOW)
		return -EINVAL;

	if (desc->convert_src != TRIG_TIMER &&
	    desc->convert_src != TRIG_EXT && desc->convert_src != TRIG_NOW)
		return -EINVAL;

	if (desc->stop_src != TRIG_COUNT && desc->stop_src != TRIG_NONE)
		return -EINVAL;

	/* Makes sure arguments are trivially compatible */
	tmp1 = desc->start_src & (TRIG_NOW | TRIG_FOLLOW | TRIG_INT);
	tmp2 = desc->start_arg;
	if (tmp1 != 0 && tmp2 != 0)
		return -EINVAL;

	tmp1 = desc->scan_begin_src & TRIG_FOLLOW;
	tmp2 = desc->scan_begin_arg;
	if (tmp1 != 0 && tmp2 != 0)
		return -EINVAL;

	tmp1 = desc->convert_src & TRIG_NOW;
	tmp2 = desc->convert_arg;
	if (tmp1 != 0 && tmp2 != 0)
		return -EINVAL;

	tmp1 = desc->stop_src & TRIG_NONE;
	tmp2 = desc->stop_arg;
	if (tmp1 != 0 && tmp2 != 0)
		return -EINVAL;

	return 0;
}

int comedi_check_specific_cmdcnt(comedi_cxt_t * cxt, comedi_cmd_t * desc)
{
	unsigned int tmp1, tmp2;
	comedi_dev_t *dev = comedi_get_dev(cxt);
	comedi_cmd_t *cmd_mask = dev->transfer->subds[desc->idx_subd]->cmd_mask;

	if (cmd_mask == NULL)
		return 0;

	if (cmd_mask->start_src != 0) {
		tmp1 = desc->start_src & ~(cmd_mask->start_src);
		tmp2 = desc->start_src & (cmd_mask->start_src);
		if (tmp1 != 0 || tmp2 == 0)
			return -EINVAL;
	}

	if (cmd_mask->scan_begin_src != 0) {
		tmp1 = desc->scan_begin_src & ~(cmd_mask->scan_begin_src);
		tmp2 = desc->scan_begin_src & (cmd_mask->scan_begin_src);
		if (tmp1 != 0 || tmp2 == 0)
			return -EINVAL;
	}

	if (cmd_mask->convert_src != 0) {
		tmp1 = desc->convert_src & ~(cmd_mask->convert_src);
		tmp2 = desc->convert_src & (cmd_mask->convert_src);
		if (tmp1 != 0 || tmp2 == 0)
			return -EINVAL;
	}

	if (cmd_mask->scan_end_src != 0) {
		tmp1 = desc->scan_end_src & ~(cmd_mask->scan_end_src);
		if (tmp1 != 0)
			return -EINVAL;
	}

	if (cmd_mask->stop_src != 0) {
		tmp1 = desc->stop_src & ~(cmd_mask->stop_src);
		tmp2 = desc->stop_src & (cmd_mask->stop_src);
		if (tmp1 != 0 || tmp2 == 0)
			return -EINVAL;
	}

	return 0;
}

/* --- IOCTL / FOPS function --- */

int comedi_ioctl_cmd(comedi_cxt_t * cxt, void *arg)
{
	int ret = 0, simul_flag = 0;
	comedi_cmd_t *cmd_desc = NULL;
	comedi_dev_t *dev = comedi_get_dev(cxt);

	comedi_loginfo("comedi_ioctl_cmd: minor=%d\n", comedi_get_minor(cxt));

	/* Allocates the command */
	cmd_desc = (comedi_cmd_t *) comedi_kmalloc(sizeof(comedi_cmd_t));
	if (cmd_desc == NULL)
		return -ENOMEM;
	memset(cmd_desc, 0, sizeof(comedi_cmd_t));

	/* Gets the command */
	ret = comedi_fill_cmddesc(cxt, cmd_desc, arg);
	if (ret != 0)
		goto out_ioctl_cmd;

	/* Checks the command */
	ret = comedi_check_cmddesc(cxt, cmd_desc);
	if (ret != 0)
		goto out_ioctl_cmd;

	ret = comedi_check_generic_cmdcnt(cmd_desc);
	if (ret != 0)
		goto out_ioctl_cmd;

	ret = comedi_check_specific_cmdcnt(cxt, cmd_desc);
	if (ret != 0)
		goto out_ioctl_cmd;

	/* Tests the command with the cmdtest function */
	if (dev->transfer->subds[cmd_desc->idx_subd]->do_cmdtest != NULL)
		ret =
		    dev->transfer->subds[cmd_desc->idx_subd]->do_cmdtest(cxt,
									 cmd_desc);
	if (ret != 0)
		goto out_ioctl_cmd;

	if (cmd_desc->flags & COMEDI_CMD_SIMUL) {
		simul_flag = 1;
		goto out_ioctl_cmd;
	}

	/* Sets the concerned subdevice as busy */
	ret = comedi_reserve_transfer(cxt, cmd_desc->idx_subd);
	if (ret < 0)
		goto out_ioctl_cmd;

	/* Gets the transfer system ready */
	comedi_init_transfer(cxt, cmd_desc);

	/* Eventually launches the command */
	ret =
	    dev->transfer->subds[cmd_desc->idx_subd]->do_cmd(cxt,
							     cmd_desc->
							     idx_subd);
	if (ret != 0) {
		comedi_cancel_transfer(cxt, cmd_desc->idx_subd);
		goto out_ioctl_cmd;
	}

      out_ioctl_cmd:
	if (ret != 0 || simul_flag == 1) {
		comedi_free_cmddesc(cmd_desc);
		comedi_kfree(cmd_desc);
	}

	return ret;
}

#endif /* !DOXYGEN_CPP */

/**
 * @file
 * Analogy for Linux, transfer related features
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
#include <linux/fs.h>
#include <asm/errno.h>

#include <analogy/context.h>
#include <analogy/device.h>

#include "proc.h"

/* --- Initialization / cleanup / cancel functions --- */

int a4l_precleanup_transfer(a4l_cxt_t * cxt)
{
	a4l_dev_t *dev;
	a4l_trf_t *tsf;
	int i, err = 0;

	__a4l_dbg(1, core_dbg, 
		  "a4l_precleanup_transfer: minor=%d\n", 
		  a4l_get_minor(cxt));

	dev = a4l_get_dev(cxt);
	tsf = &dev->transfer;

	if (tsf == NULL) {
		__a4l_err("a4l_precleanup_transfer: "
			  "incoherent status, transfer block not reachable\n");
		return -ENODEV;
	}

	for (i = 0; i < tsf->nb_subd; i++) {

		if (test_bit(A4L_TSF_MMAP, &(tsf->status[i]))) {
			__a4l_err("a4l_precleanup_transfer: "
				  "device busy, buffer must be unmapped\n");
			err = -EPERM;
			goto out_error;
		}

		if (test_and_set_bit(A4L_TSF_BUSY, &(tsf->status[i]))) {
			__a4l_err("a4l_precleanup_transfer: "
				  "device busy, acquisition occuring\n");
			err = -EBUSY;
			goto out_error;
		} else
			set_bit(A4L_TSF_CLEAN, &(tsf->status[i]));
	}

	return 0;

out_error:
	for (i = 0; i < tsf->nb_subd; i++) {

		if (test_bit(A4L_TSF_CLEAN, &(tsf->status[i]))){
			clear_bit(A4L_TSF_BUSY, &(tsf->status[i]));
			clear_bit(A4L_TSF_CLEAN, &(tsf->status[i]));
		}
	}

	return err;
}

int a4l_cleanup_transfer(a4l_cxt_t * cxt)
{
	a4l_dev_t *dev;
	a4l_trf_t *tsf;
	int i;

	__a4l_dbg(1, core_dbg, 
		  "a4l_cleanup_transfer: minor=%d\n", 
		  a4l_get_minor(cxt));

	dev = a4l_get_dev(cxt);
	tsf = &dev->transfer;

	/* Releases the various buffers */
	if (tsf->status != NULL)
		rtdm_free(tsf->status);

	if (tsf->bufs != NULL) {
		for (i = 0; i < tsf->nb_subd; i++) {
			if (tsf->bufs[i] != NULL) {
				a4l_free_buffer(tsf->bufs[i]);
				a4l_cleanup_sync(&tsf->bufs[i]->sync);
				rtdm_free(tsf->bufs[i]);
			}
		}
		rtdm_free(tsf->bufs);
	}

	/* Releases the pointers tab, if need be */
	if (tsf->subds != NULL) {
		rtdm_free(tsf->subds);
	}

	return 0;
}

void a4l_presetup_transfer(a4l_cxt_t *cxt)
{
	a4l_dev_t *dev = NULL;
	a4l_trf_t *tsf;

	__a4l_dbg(1, core_dbg, 
		  "a4l_presetup_transfer: minor=%d\n",
		  a4l_get_minor(cxt));

	dev = a4l_get_dev(cxt);
	tsf = &dev->transfer;

	/* Clear the structure */
	memset(tsf, 0, sizeof(a4l_trf_t));

	/* We consider 0 can be valid index */
	tsf->idx_read_subd = A4L_IDX_UNUSED;
	tsf->idx_write_subd = A4L_IDX_UNUSED;

	/* 0 is also considered as a valid IRQ, then 
	   the IRQ number must be initialized with another value */
	tsf->irq_desc.irq = A4L_IRQ_UNUSED;
}

int a4l_setup_transfer(a4l_cxt_t * cxt)
{
	a4l_dev_t *dev = NULL;
	a4l_trf_t *tsf;
	a4l_subd_t *subd;
	struct list_head *this;
	int i = 0, ret = 0;

	__a4l_dbg(1, core_dbg, 
		  "a4l_setup_transfer: minor=%d\n",
		  a4l_get_minor(cxt));

	dev = a4l_get_dev(cxt);
	tsf = &dev->transfer;

	/* Recovers the subdevices count 
	   (as they are registered in a linked list */
	list_for_each(this, &dev->subdvsq) {
		tsf->nb_subd++;
	}

	/* Allocates a suitable tab for the subdevices */
	tsf->subds = rtdm_malloc(tsf->nb_subd * sizeof(a4l_subd_t *));
	if (tsf->subds == NULL) {
		__a4l_err("a4l_setup_transfer: call1(alloc) failed \n");
		ret = -ENOMEM;
		goto out_setup_tsf;
	}

	/* Recovers the subdevices pointers */
	list_for_each(this, &dev->subdvsq) {
		subd = list_entry(this, a4l_subd_t, list);

		if (subd->flags & A4L_SUBD_AI)
			tsf->idx_read_subd = i;

		if (subd->flags & A4L_SUBD_AO)
			tsf->idx_write_subd = i;

		tsf->subds[i++] = subd;
	}

	/* Allocates various buffers */
	tsf->bufs = rtdm_malloc(tsf->nb_subd * sizeof(a4l_buf_t *));
	if (tsf->bufs == NULL) {
		__a4l_err("a4l_setup_transfer: call2(alloc) failed \n");
		ret = -ENOMEM;
		goto out_setup_tsf;
	}
	memset(tsf->bufs, 0, tsf->nb_subd * sizeof(a4l_buf_t *));

	for (i = 0; i < tsf->nb_subd; i++) {
		if (tsf->subds[i]->flags & A4L_SUBD_CMD) {
			tsf->bufs[i] = rtdm_malloc(sizeof(a4l_buf_t));
			if (tsf->bufs[i] == NULL) {
				__a4l_err("a4l_setup_transfer: "
					  "call3(alloc) failed \n");
				ret = -ENOMEM;
				goto out_setup_tsf;
			}

			memset(tsf->bufs[i], 0, sizeof(a4l_buf_t));
			a4l_init_sync(&(tsf->bufs[i]->sync));

			if ((ret = a4l_alloc_buffer(tsf->bufs[i])) != 0)
				goto out_setup_tsf;
		}
	}

	tsf->status = rtdm_malloc(tsf->nb_subd * sizeof(unsigned long));
	if (tsf->status == NULL) {
		__a4l_err("a4l_setup_transfer: call4(alloc) failed \n");
		ret = -ENOMEM;
	}

	memset(tsf->status, 0, tsf->nb_subd * sizeof(unsigned long));

out_setup_tsf:

	if (ret != 0)
		a4l_cleanup_transfer(cxt);

	return ret;
}

int a4l_reserve_transfer(a4l_cxt_t * cxt, int idx_subd)
{
	a4l_dev_t *dev = a4l_get_dev(cxt);

	__a4l_dbg(1, core_dbg,
		  "a4l_reserve_transfer: minor=%d idx=%d\n",
		  a4l_get_minor(cxt), idx_subd);

	if (test_and_set_bit(A4L_TSF_BUSY,
			     &(dev->transfer.status[idx_subd]))) {
		__a4l_err("a4l_reserve_transfer: device currently busy\n");
		return -EBUSY;
	}

	return 0;
}

int a4l_init_transfer(a4l_cxt_t * cxt, a4l_cmd_t * cmd)
{
	int i;
	a4l_dev_t *dev = a4l_get_dev(cxt);

	__a4l_dbg(1, core_dbg,
		  "a4l_init_transfer: minor=%d idx=%d\n",
		  a4l_get_minor(cxt), cmd->idx_subd);

	/* Checks if the transfer system has to work in bulk mode */
	if (cmd->flags & A4L_CMD_BULK)
		set_bit(A4L_TSF_BULK,
			&(dev->transfer.status[cmd->idx_subd]));

	/* Sets the working command */
	dev->transfer.bufs[cmd->idx_subd]->cur_cmd = cmd;

	/* Initializes the counts */
	dev->transfer.bufs[cmd->idx_subd]->end_count = 0;
	dev->transfer.bufs[cmd->idx_subd]->prd_count = 0;
	dev->transfer.bufs[cmd->idx_subd]->cns_count = 0;
	dev->transfer.bufs[cmd->idx_subd]->tmp_count = 0;
	dev->transfer.bufs[cmd->idx_subd]->mng_count = 0;

	/* Flush pending events */
	dev->transfer.bufs[cmd->idx_subd]->evt_flags = 0;
	a4l_flush_sync(&dev->transfer.bufs[cmd->idx_subd]->sync);

	/* Computes the count to reach, if need be */
	if (cmd->stop_src == TRIG_COUNT) {
		for (i = 0; i < cmd->nb_chan; i++) {
			a4l_chan_t *chft;
			chft = a4l_get_chfeat(dev->transfer.
					      subds[cmd->idx_subd],
					      CR_CHAN(cmd->chan_descs[i]));
			dev->transfer.bufs[cmd->idx_subd]->end_count +=
				chft->nb_bits / 8;
		}
		dev->transfer.bufs[cmd->idx_subd]->end_count *= cmd->stop_arg;
	}

	/* Always returning 0 is here useless... for the moment */
	return 0;
}

int a4l_cancel_transfer(a4l_cxt_t * cxt, int idx_subd)
{
	int ret = 0;
	a4l_subd_t *subd;
	a4l_dev_t *dev = a4l_get_dev(cxt);

	/* Basic checking */
	if (!test_bit(A4L_TSF_BUSY, &(dev->transfer.status[idx_subd])))
		return 0;

	/* Retrieves the proper subdevice pointer */
	subd = dev->transfer.subds[idx_subd];

	/* If a "cancel" function is registered, call it
	   (Note: this function is called before having checked 
	   if a command is under progress; we consider that 
	   the "cancel" function can be used as as to (re)initialize 
	   some component) */
	if (subd->cancel != NULL && (ret = subd->cancel(subd)) < 0) {
		__a4l_err("a4l_cancel: "
			  "subdevice %d cancel handler failed (ret=%d)\n",
			  idx_subd, ret);
	}

	/* Clears the "busy" flag */
	clear_bit(A4L_TSF_BUSY, &(dev->transfer.status[idx_subd]));

	/* If the subdevice is command capable and 
	   if a command is under progress, 
	   disable it and free it... */
	if (dev->transfer.bufs != NULL &&
	    dev->transfer.bufs[idx_subd] != NULL &&
	    dev->transfer.bufs[idx_subd]->cur_cmd != NULL) {

		a4l_free_cmddesc(dev->transfer.bufs[idx_subd]->cur_cmd);
		rtdm_free(dev->transfer.bufs[idx_subd]->cur_cmd);
		dev->transfer.bufs[idx_subd]->cur_cmd = NULL;

		/* ...we must also clean the events flags */
		dev->transfer.bufs[idx_subd]->evt_flags = 0;
	}

	return ret;
}

int a4l_cancel_transfers(a4l_cxt_t * cxt)
{
	a4l_dev_t *dev = a4l_get_dev(cxt);
	int i, ret = 0;

	/* The caller of a4l_cancel_transfers is bound not to have
	   checked whether the subdevice was attached; so we do it here */
	if (!test_bit(A4L_DEV_ATTACHED, &dev->flags))
		return 0;

	for (i = 0; i < dev->transfer.nb_subd && ret == 0; i++)
		ret = a4l_cancel_transfer(cxt, i);

	return ret;
}

/* --- IRQ handling section --- */

int a4l_request_irq(a4l_dev_t * dev,
		    unsigned int irq,
		    a4l_irq_hdlr_t handler,
		    unsigned long flags, void *cookie)
{
	int ret;
	unsigned long __flags;

	if (dev->transfer.irq_desc.irq != A4L_IRQ_UNUSED)
		return -EBUSY;

	/* A spinlock is used so as to prevent race conditions 
	   on the field "irq" of the IRQ descriptor 
	   (even if such a case is bound not to happen) */
	a4l_lock_irqsave(&dev->lock, __flags);

	ret = __a4l_request_irq(&dev->transfer.irq_desc,
				irq, handler, flags, cookie);

	if (ret != 0) {
		__a4l_err("a4l_request_irq: IRQ registration failed\n");
		dev->transfer.irq_desc.irq = A4L_IRQ_UNUSED;
	}

	a4l_unlock_irqrestore(&dev->lock, __flags);

	return ret;
}

int a4l_free_irq(a4l_dev_t * dev, unsigned int irq)
{

	int ret = 0;

	if (dev->transfer.irq_desc.irq != irq)
		return -EINVAL;

	/* There is less need to use a spinlock 
	   than for a4l_request_irq() */
	ret = __a4l_free_irq(&dev->transfer.irq_desc);

	if (ret == 0)
		dev->transfer.irq_desc.irq = A4L_IRQ_UNUSED;

	return 0;
}

unsigned int a4l_get_irq(a4l_dev_t * dev)
{
	return dev->transfer.irq_desc.irq;
}

/* --- Proc section --- */

#ifdef CONFIG_PROC_FS

int a4l_rdproc_transfer(char *page,
			char **start,
			off_t off, int count, int *eof, void *data)
{
	int i, len = 0;
	char *p = page;
	a4l_trf_t *transfer = (a4l_trf_t *) data;

	p += sprintf(p, "--  Subdevices --\n\n");
	p += sprintf(p, "| idx | type\n");

	/* Gives the subdevice type's name */
	for (i = 0; i < transfer->nb_subd; i++) {
		char *type;
		switch (transfer->subds[i]->flags & A4L_SUBD_TYPES) {
		case A4L_SUBD_UNUSED:
			type = "Unused subdevice";
			break;
		case A4L_SUBD_AI:
			type = "Analog input subdevice";
			break;
		case A4L_SUBD_AO:
			type = "Analog output subdevice";
			break;
		case A4L_SUBD_DI:
			type = "Digital input subdevice";
			break;
		case A4L_SUBD_DO:
			type = "Digital output subdevice";
			break;
		case A4L_SUBD_DIO:
			type = "Digital input/output subdevice";
			break;
		case A4L_SUBD_COUNTER:
			type = "Counter subdevice";
			break;
		case A4L_SUBD_TIMER:
			type = "Timer subdevice";
			break;
		case A4L_SUBD_MEMORY:
			type = "Memory subdevice";
			break;
		case A4L_SUBD_CALIB:
			type = "Calibration subdevice";
			break;
		case A4L_SUBD_PROC:
			type = "Processor subdevice";
			break;
		case A4L_SUBD_SERIAL:
			type = "Serial subdevice";
			break;
		default:
			type = "Unknown subdevice";
		}

		p += sprintf(p, "|  %02d | %s\n", i, type);
	}

	/* Handles any proc-file reading way */
	len = p - page - off;
	/* If the requested size is greater than we provide,
	   the read operation is over */
	if (len <= off + count)
		*eof = 1;
	/* In case the read operation is performed in many steps,
	   the start pointer must be redefined */
	*start = page + off;
	/* If the requested size is lower than we provide,
	   the read operation will be done in more than one step */
	if (len > count)
		len = count;
	/* In case the offset is not correct (too high) */
	if (len < 0)
		len = 0;

	return len;
}

#endif /* CONFIG_PROC_FS */

/* --- IOCTL / FOPS functions --- */

int a4l_ioctl_cancel(a4l_cxt_t * cxt, void *arg)
{
	unsigned int idx_subd = (unsigned long)arg;
	a4l_dev_t *dev = a4l_get_dev(cxt);
	a4l_subd_t *subd;

	__a4l_dbg(1, core_dbg, 
		  "a4l_ioctl_cancel: minor=%d\n", a4l_get_minor(cxt));

	/* Basically check the device */
	if (!test_bit(A4L_DEV_ATTACHED, &dev->flags)) {
		__a4l_err("a4l_ioctl_cancel: operation not supported on "
			  "an unattached device\n");
		return -EINVAL;
	}

	if (idx_subd >= dev->transfer.nb_subd) {
		__a4l_err("a4l_ioctl_cancel: bad subdevice index\n");
		return -EINVAL;
	}

	if ((dev->transfer.subds[idx_subd]->flags & A4L_SUBD_TYPES) == 
	    A4L_SUBD_UNUSED) {
		__a4l_err("a4l_ioctl_cancel: non functional subdevice\n");
		return -EIO;
	}

	if (!(dev->transfer.subds[idx_subd]->flags & A4L_SUBD_CMD)) {
		__a4l_err("a4l_ioctl_cancel: operation not supported, "
			  "synchronous only subdevice\n");
		return -EIO;
	}

	subd = dev->transfer.subds[idx_subd];

	if (!test_bit(A4L_TSF_BUSY, &(dev->transfer.status[idx_subd]))) {
		__a4l_err("a4l_ioctl_cancel: subdevice currently idle\n");
		return -EINVAL;
	}

	return a4l_cancel_transfer(cxt, idx_subd);
}

#endif /* !DOXYGEN_CPP */

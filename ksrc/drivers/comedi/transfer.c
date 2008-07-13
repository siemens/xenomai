/**
 * @file
 * Comedi for RTDM, transfer related features
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

#include <comedi/context.h>
#include <comedi/device.h>

#include "proc.h"

/* --- Initialization / cleanup / cancel functions --- */

int comedi_cleanup_transfer(comedi_cxt_t * cxt)
{
	comedi_dev_t *dev;
	comedi_trf_t *tsf;
	int i;

	comedi_loginfo("comedi_cleanup_transfer: minor=%d\n",
		       comedi_get_minor(cxt));

	dev = comedi_get_dev(cxt);
	tsf = dev->transfer;

	if (tsf == NULL)
		return -ENODEV;

	for (i = 0; i < tsf->nb_subd; i++) {
		if (test_bit(COMEDI_TSF_BUSY, &(tsf->status[i])))
			return EBUSY;

		if (test_bit(COMEDI_TSF_MMAP, &(tsf->status[i])))
			return -EPERM;
	}

	/* Releases the various buffers */
	if (tsf->status != NULL)
		comedi_kfree(tsf->status);

	if (tsf->bufs != NULL) {
		for (i = 0; i < tsf->nb_subd; i++) {
			if (tsf->bufs[i] != NULL) {
				comedi_free_buffer(tsf->bufs[i]);
				comedi_cleanup_sync(&tsf->bufs[i]->sync);
				comedi_kfree(tsf->bufs[i]);
			}
		}
		comedi_kfree(tsf->bufs);
	}

	if (tsf->subds != NULL) {

		/* If the driver is dynamic, the subdevices
		   structures must be freed at transfer cleanup time */
		if ((dev->driver->flags & COMEDI_DYNAMIC_DRV) != 0) {
			for (i = 0; i < tsf->nb_subd; i++)
				comedi_kfree(tsf->subds[i]);
		}

		/* Releases the pointers tab */
		comedi_kfree(tsf->subds);
	}

	comedi_kfree(tsf);
	dev->transfer = NULL;

	return 0;
}

int comedi_setup_transfer(comedi_cxt_t * cxt)
{
	comedi_dev_t *dev = NULL;
	comedi_trf_t *tsf;
	comedi_drv_t *drv;
	comedi_subd_t *subd;
	struct list_head *this;
	int i = 0, ret = 0;

	comedi_loginfo("comedi_setup_transfer: minor=%d\n",
		       comedi_get_minor(cxt));

	dev = comedi_get_dev(cxt);
	drv = dev->driver;

	/* Allocates the main structure */
	tsf = comedi_kmalloc(sizeof(comedi_trf_t));
	if (tsf == NULL) {
		comedi_logerr("comedi_setup_transfer: call1(alloc) failed \n");
		return -ENOMEM;
	}
	memset(tsf, 0, sizeof(comedi_trf_t));

	/* We consider 0 can be valid index */
	tsf->idx_read_subd = COMEDI_IDX_UNUSED;
	tsf->idx_write_subd = COMEDI_IDX_UNUSED;

	/* 0 is also considered as a valid IRQ, then 
	   the IRQ number must be initialized with another value */
	tsf->irq_desc.irq = COMEDI_IRQ_UNUSED;

	dev->transfer = tsf;

	/* Recovers the subdevices count 
	   (as they are registered in a linked list */
	list_for_each(this, &drv->subdvsq) {
		tsf->nb_subd++;
	}

	/* Allocates a suitable tab for the subdevices */
	tsf->subds = comedi_kmalloc(tsf->nb_subd * sizeof(comedi_subd_t *));
	if (tsf->subds == NULL) {
		comedi_logerr("comedi_setup_transfer: call2(alloc) failed \n");
		ret = -ENOMEM;
		goto out_setup_tsf;
	}

	/* Recovers the subdevices pointers */
	list_for_each(this, &drv->subdvsq) {
		subd = list_entry(this, comedi_subd_t, list);

		if (subd->flags & COMEDI_SUBD_AI)
			tsf->idx_read_subd = i;

		if (subd->flags & COMEDI_SUBD_AO)
			tsf->idx_write_subd = i;

		tsf->subds[i++] = subd;
	}

	/* Allocates various buffers */
	tsf->bufs = comedi_kmalloc(tsf->nb_subd * sizeof(comedi_buf_t *));
	if (tsf->bufs == NULL) {
		ret = -ENOMEM;
		goto out_setup_tsf;
	}
	memset(tsf->bufs, 0, tsf->nb_subd * sizeof(comedi_buf_t *));

	for (i = 0; i < tsf->nb_subd; i++) {
		if (tsf->subds[i]->flags & COMEDI_SUBD_CMD) {
			tsf->bufs[i] = comedi_kmalloc(sizeof(comedi_buf_t));
			if (tsf->bufs[i] == NULL) {
				comedi_logerr
				    ("comedi_setup_transfer: call5-6(alloc) failed \n");
				ret = -ENOMEM;
				goto out_setup_tsf;
			}

			memset(tsf->bufs[i], 0, sizeof(comedi_buf_t));
			comedi_init_sync(&(tsf->bufs[i]->sync));

			if ((ret = comedi_alloc_buffer(tsf->bufs[i])) != 0)
				goto out_setup_tsf;
		}
	}

	tsf->status = comedi_kmalloc(tsf->nb_subd * sizeof(unsigned long));
	if (tsf->status == NULL) {
		comedi_logerr("comedi_setup_transfer: call8(alloc) failed \n");
		ret = -ENOMEM;
	}

	memset(tsf->status, 0, tsf->nb_subd * sizeof(unsigned long));

      out_setup_tsf:

	if (ret != 0)
		comedi_cleanup_transfer(cxt);

	/* If the driver is dynamic, the subdevices are 
	   added during attachment; then there must be no 
	   subdevices in the list for the next attachment */
	if ((drv->flags & COMEDI_DYNAMIC_DRV) != 0) {
		while (&drv->subdvsq != drv->subdvsq.next) {
			this = drv->subdvsq.next;
			subd = list_entry(this, comedi_subd_t, list);
			list_del(this);
			comedi_kfree(subd);
		}
	}

	return ret;
}

int comedi_reserve_transfer(comedi_cxt_t * cxt, int idx_subd)
{
	comedi_dev_t *dev = comedi_get_dev(cxt);

	comedi_loginfo("comedi_reserve_transfer: minor=%d idx=%d\n",
		       comedi_get_minor(cxt), idx_subd);

	if (test_and_set_bit(COMEDI_TSF_BUSY,
			     &(dev->transfer->status[idx_subd])))
		return -EBUSY;

	return 0;
}

int comedi_init_transfer(comedi_cxt_t * cxt, comedi_cmd_t * cmd)
{
	int i;
	comedi_dev_t *dev = comedi_get_dev(cxt);

	comedi_loginfo("comedi_init_transfer: minor=%d idx=%d\n",
		       comedi_get_minor(cxt), cmd->idx_subd);

	/* Checks if the transfer system has to work in bulk mode */
	if (cmd->flags & COMEDI_CMD_BULK)
		set_bit(COMEDI_TSF_BULK,
			&(dev->transfer->status[cmd->idx_subd]));

	/* Sets the working command */
	dev->transfer->bufs[cmd->idx_subd]->cur_cmd = cmd;

	/* Initializes the counts and the flag variable */
	dev->transfer->bufs[cmd->idx_subd]->end_count = 0;
	dev->transfer->bufs[cmd->idx_subd]->prd_count = 0;
	dev->transfer->bufs[cmd->idx_subd]->cns_count = 0;
	dev->transfer->bufs[cmd->idx_subd]->tmp_count = 0;
	dev->transfer->bufs[cmd->idx_subd]->evt_flags = 0;
	dev->transfer->bufs[cmd->idx_subd]->mng_count = 0;

	/* Computes the count to reach, if need be */
	if (cmd->stop_src == TRIG_COUNT) {
		for (i = 0; i < cmd->nb_chan; i++) {
			comedi_chan_t *chft;
			chft =
			    comedi_get_chfeat(dev->transfer->
					      subds[cmd->idx_subd],
					      CR_CHAN(cmd->chan_descs[i]));
			dev->transfer->bufs[cmd->idx_subd]->end_count +=
			    chft->nb_bits / 8;
		}
		dev->transfer->bufs[cmd->idx_subd]->end_count *= cmd->stop_arg;
	}

	/* Always returning 0 is here useless... for the moment */
	return 0;
}

int comedi_cancel_transfer(comedi_cxt_t * cxt, int idx_subd)
{
	int ret = 0;
	comedi_subd_t *subd;
	comedi_dev_t *dev = comedi_get_dev(cxt);

	/* Basic checking */
	if (!test_bit(COMEDI_TSF_BUSY, &(dev->transfer->status[idx_subd])))
		return 0;

	/* Retrieves the proper subdevice pointer */
	subd = dev->transfer->subds[idx_subd];

	/* If a "cancel" function is registered, call it
	   (Note: this function is called before having checked 
	   if a command is under progress; we consider that 
	   the "cancel" function can be used as as to (re)initialize 
	   some component) */
	if (subd->cancel != NULL && (ret = subd->cancel(cxt, idx_subd)) < 0) {
		comedi_logerr
		    ("comedi_cancel: subdevice %d cancel handler failed (ret=%d)\n",
		     idx_subd, ret);
	}

	/* Clears the "busy" flag */
	clear_bit(COMEDI_TSF_BUSY, &(dev->transfer->status[idx_subd]));

	/* If the subdevice is command capable and 
	   if there is a command is under progress, 
	   disable it and free it... */
	if (dev->transfer->bufs != NULL &&
	    dev->transfer->bufs[idx_subd]->cur_cmd != NULL) {

		comedi_free_cmddesc(dev->transfer->bufs[idx_subd]->cur_cmd);
		comedi_kfree(dev->transfer->bufs[idx_subd]->cur_cmd);
		dev->transfer->bufs[idx_subd]->cur_cmd = NULL;

		/* ...we must also clean the events flags */
		dev->transfer->bufs[idx_subd]->evt_flags = 0;
	}

	return ret;
}

/* --- IRQ handling section --- */

int comedi_request_irq(comedi_dev_t * dev,
		       unsigned int irq,
		       comedi_irq_hdlr_t handler,
		       unsigned long flags, void *cookie)
{
	int ret;
	unsigned long __flags;

	if (dev->transfer->irq_desc.irq != COMEDI_IRQ_UNUSED)
		return -EBUSY;

	/* A spinlock is used so as to prevent race conditions 
	   on the field "irq" of the IRQ descriptor 
	   (even if such a case is bound not to happen) */
	comedi_lock_irqsave(&dev->lock, __flags);

	ret = __comedi_request_irq(&dev->transfer->irq_desc,
				   irq, handler, flags, cookie);

	if (ret != 0)
		dev->transfer->irq_desc.irq = COMEDI_IRQ_UNUSED;

	comedi_unlock_irqrestore(&dev->lock, __flags);

	return ret;
}

int comedi_free_irq(comedi_dev_t * dev, unsigned int irq)
{

	int ret = 0;

	if (dev->transfer->irq_desc.irq != irq)
		return -EINVAL;

	/* There is less need to use a spinlock 
	   than for comedi_request_irq() */
	ret = __comedi_free_irq(&dev->transfer->irq_desc);

	if (ret == 0)
		dev->transfer->irq_desc.irq = COMEDI_IRQ_UNUSED;

	return 0;
}

unsigned int comedi_get_irq(comedi_dev_t * dev)
{
	return dev->transfer->irq_desc.irq;
}

/* --- Proc section --- */

#ifdef CONFIG_PROC_FS

int comedi_rdproc_transfer(char *page,
			   char **start,
			   off_t off, int count, int *eof, void *data)
{
	int i, len = 0;
	char *p = page;
	comedi_trf_t *transfer = (comedi_trf_t *) data;

	p += sprintf(p, "--  Subdevices --\n\n");
	p += sprintf(p, "| idx | type\n");

	/* Gives the subdevice type's name */
	for (i = 0; i < transfer->nb_subd; i++) {
		char *type;
		switch (transfer->subds[i]->flags & COMEDI_SUBD_TYPES) {
		case COMEDI_SUBD_UNUSED:
			type = "Unused subdevice";
			break;
		case COMEDI_SUBD_AI:
			type = "Analog input subdevice";
			break;
		case COMEDI_SUBD_AO:
			type = "Analog output subdevice";
			break;
		case COMEDI_SUBD_DI:
			type = "Digital input subdevice";
			break;
		case COMEDI_SUBD_DO:
			type = "Digital output subdevice";
			break;
		case COMEDI_SUBD_DIO:
			type = "Digital input/output subdevice";
			break;
		case COMEDI_SUBD_COUNTER:
			type = "Counter subdevice";
			break;
		case COMEDI_SUBD_TIMER:
			type = "Timer subdevice";
			break;
		case COMEDI_SUBD_MEMORY:
			type = "Memory subdevice";
			break;
		case COMEDI_SUBD_CALIB:
			type = "Calibration subdevice";
			break;
		case COMEDI_SUBD_PROC:
			type = "Processor subdevice";
			break;
		case COMEDI_SUBD_SERIAL:
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

int comedi_ioctl_cancel(comedi_cxt_t * cxt, void *arg)
{
	unsigned int idx_subd = (unsigned long)arg;
	comedi_dev_t *dev = comedi_get_dev(cxt);
	comedi_subd_t *subd;

	if (idx_subd >= dev->transfer->nb_subd)
		return -EINVAL;

	if (dev->transfer->subds[idx_subd]->flags & COMEDI_SUBD_UNUSED)
		return -EIO;

	if (!(dev->transfer->subds[idx_subd]->flags & COMEDI_SUBD_CMD))
		return -EIO;

	subd = dev->transfer->subds[idx_subd];

	if (!test_bit(COMEDI_TSF_BUSY, &(dev->transfer->status[idx_subd])))
		return -EINVAL;

	return comedi_cancel_transfer(cxt, idx_subd);
}

#endif /* !DOXYGEN_CPP */

/**
 * @file
 * Analogy for Linux, buffer related features
 *
 * @note Copyright (C) 1997-2000 David A. Schleef <ds@schleef.org>
 * @note Copyright (C) 2008 Alexis Berlemont <alexis.berlemont@free.fr>
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
#include <linux/mman.h>
#include <linux/vmalloc.h>
#include <asm/errno.h>
#include <rtdm/rtdm_driver.h>
#include <analogy/context.h>
#include <analogy/device.h>
#include <analogy/buffer.h>
#include <analogy/transfer.h>

/* --- Buffer allocation / free functions --- */

void a4l_free_buffer(a4l_buf_t * buf_desc)
{
	if (buf_desc->pg_list != NULL) {
		rtdm_free(buf_desc->pg_list);
		buf_desc->pg_list = NULL;
	}

	if (buf_desc->buf != NULL) {
		char *vaddr, *vabase = buf_desc->buf;
		for (vaddr = vabase; vaddr < vabase + buf_desc->size;
		     vaddr += PAGE_SIZE)
			ClearPageReserved(vmalloc_to_page(vaddr));
		vfree(buf_desc->buf);
		buf_desc->buf = NULL;
	}
}

int a4l_alloc_buffer(a4l_buf_t * buf_desc)
{
	int ret = 0;
	char *vaddr, *vabase;

	if (buf_desc->size == 0)
		buf_desc->size = A4L_BUF_DEFSIZE;

	buf_desc->size = PAGE_ALIGN(buf_desc->size);

	buf_desc->buf = vmalloc(buf_desc->size);

	if (buf_desc->buf == NULL) {
		ret = -ENOMEM;
		goto out_virt_contig_alloc;
	}

	vabase = buf_desc->buf;

	for (vaddr = vabase; vaddr < vabase + buf_desc->size;
	     vaddr += PAGE_SIZE)
		SetPageReserved(vmalloc_to_page(vaddr));

	buf_desc->pg_list = rtdm_malloc(((buf_desc->size) >> PAGE_SHIFT) *
					sizeof(unsigned long));
	if (buf_desc->pg_list == NULL) {
		ret = -ENOMEM;
		goto out_virt_contig_alloc;
	}

	for (vaddr = vabase; vaddr < vabase + buf_desc->size;
	     vaddr += PAGE_SIZE)
		buf_desc->pg_list[(vaddr - vabase) >> PAGE_SHIFT] =
			(unsigned long) page_to_phys(vmalloc_to_page(vaddr));

out_virt_contig_alloc:
	if (ret != 0)
		a4l_free_buffer(buf_desc);

	return ret;
}

/* --- Current Command management function --- */

a4l_cmd_t *a4l_get_cmd(a4l_subd_t *subd)
{
	a4l_dev_t *dev = subd->dev;

	/* Check that subdevice supports commands */
	if (dev->transfer.bufs == NULL)
		return NULL;

	return dev->transfer.bufs[subd->idx]->cur_cmd;
}

/* --- Munge related function --- */

int a4l_get_chan(a4l_subd_t *subd)
{
	a4l_dev_t *dev = subd->dev;
	int i, j, tmp_count, tmp_size = 0;	
	a4l_cmd_t *cmd;

	/* Check that subdevice supports commands */
	if (dev->transfer.bufs == NULL)
		return -EINVAL;

	/* Check a command is executed */
	if (dev->transfer.bufs[subd->idx]->cur_cmd == NULL)
		return -EINVAL;

	/* Retrieve the proper command descriptor */
	cmd = dev->transfer.bufs[subd->idx]->cur_cmd;

	/* There is no need to check the channel idx, 
	   it has already been controlled in command_test */

	/* We assume channels can have different sizes;
	   so, we have to compute the global size of the channels
	   in this command... */
	for (i = 0; i < cmd->nb_chan; i++) {
		j = (subd->chan_desc->mode != A4L_CHAN_GLOBAL_CHANDESC) ? 
			CR_CHAN(cmd->chan_descs[i]) : 0;
		tmp_size += subd->chan_desc->chans[j].nb_bits;
	}

	/* Translation bits -> bytes */
	tmp_size /= 8;

	tmp_count = dev->transfer.bufs[subd->idx]->mng_count % tmp_size;

	/* Translation bytes -> bits */
	tmp_count *= 8;

	/* ...and find the channel the last munged sample 
	   was related with */
	for (i = 0; tmp_count > 0 && i < cmd->nb_chan; i++) {
		j = (subd->chan_desc->mode != A4L_CHAN_GLOBAL_CHANDESC) ? 
			CR_CHAN(cmd->chan_descs[i]) : 0;
		tmp_count -= subd->chan_desc->chans[j].nb_bits;
	}

	if (tmp_count == 0)
		return i;
	else
		return -EINVAL;
}

/* --- Transfer / copy functions --- */

int a4l_buf_prepare_absput(a4l_subd_t *subd, unsigned long count)
{
	a4l_dev_t *dev;
	a4l_buf_t *buf;
	
	if ((subd->flags & A4L_SUBD_MASK_READ) == 0)
		return -EINVAL;

	dev = subd->dev;
	buf = dev->transfer.bufs[subd->idx];

	return __pre_abs_put(buf, count);
}


int a4l_buf_commit_absput(a4l_subd_t *subd, unsigned long count)
{
	a4l_dev_t *dev;
	a4l_buf_t *buf;
	
	if ((subd->flags & A4L_SUBD_MASK_READ) == 0)
		return -EINVAL;

	dev = subd->dev;
	buf = dev->transfer.bufs[subd->idx];

	return __abs_put(buf, count);
}

int a4l_buf_prepare_put(a4l_subd_t *subd, unsigned long count)
{
	a4l_dev_t *dev;
	a4l_buf_t *buf;
	
	if ((subd->flags & A4L_SUBD_MASK_READ) == 0)
		return -EINVAL;

	dev = subd->dev;
	buf = dev->transfer.bufs[subd->idx];

	return __pre_put(buf, count);
}

int a4l_buf_commit_put(a4l_subd_t *subd, unsigned long count)
{
	a4l_dev_t *dev;
	a4l_buf_t *buf;
	
	if ((subd->flags & A4L_SUBD_MASK_READ) == 0)
		return -EINVAL;

	dev = subd->dev;
	buf = dev->transfer.bufs[subd->idx];

	return __put(buf, count);	
}

int a4l_buf_put(a4l_subd_t *subd, void *bufdata, unsigned long count)
{
	int err;
	a4l_dev_t *dev;
	a4l_buf_t *buf;
	
	if ((subd->flags & A4L_SUBD_MASK_READ) == 0)
		return -EINVAL;

	dev = subd->dev;
	buf = dev->transfer.bufs[subd->idx];

	if (__count_to_put(buf) < count)
		return -EAGAIN;

	err = __produce(NULL, buf, bufdata, count);
	if (err < 0)
		return err;	

	err = __put(buf, count);

	return err;
}

int a4l_buf_prepare_absget(a4l_subd_t *subd, unsigned long count)
{
	a4l_dev_t *dev;
	a4l_buf_t *buf;
	
	if ((subd->flags & A4L_SUBD_MASK_WRITE) == 0)
		return -EINVAL;

	dev = subd->dev;
	buf = dev->transfer.bufs[subd->idx];

	return __pre_abs_get(buf, count);
}

int a4l_buf_commit_absget(a4l_subd_t *subd, unsigned long count)
{
	a4l_dev_t *dev;
	a4l_buf_t *buf;
	
	if ((subd->flags & A4L_SUBD_MASK_WRITE) == 0)
		return -EINVAL;

	dev = subd->dev;
	buf = dev->transfer.bufs[subd->idx];

	return __abs_get(buf, count);
}

int a4l_buf_prepare_get(a4l_subd_t *subd, unsigned long count)
{
	a4l_dev_t *dev;
	a4l_buf_t *buf;
	
	if ((subd->flags & A4L_SUBD_MASK_WRITE) == 0)
		return -EINVAL;

	dev = subd->dev;
	buf = dev->transfer.bufs[subd->idx];

	return __pre_get(buf, count);
}

int a4l_buf_commit_get(a4l_subd_t *subd, unsigned long count)
{
	a4l_dev_t *dev;
	a4l_buf_t *buf;
	
	if ((subd->flags & A4L_SUBD_MASK_WRITE) == 0)
		return -EINVAL;

	dev = subd->dev;
	buf = dev->transfer.bufs[subd->idx];

	return __get(buf, count);
}

int a4l_buf_get(a4l_subd_t *subd, void *bufdata, unsigned long count)
{
	int err;
	a4l_dev_t *dev;
	a4l_buf_t *buf;
	
	if ((subd->flags & A4L_SUBD_MASK_WRITE) == 0)
		return -EINVAL;

	dev = subd->dev;
	buf = dev->transfer.bufs[subd->idx];

	if (__count_to_get(buf) < count)
		return -EAGAIN;

	err = __consume(NULL, buf, bufdata, count);
	if (err < 0)
		return err;

	err = __get(buf, count);

	return err;
}

int a4l_buf_evt(a4l_subd_t *subd, unsigned long evts)
{
	a4l_dev_t *dev = subd->dev;
	a4l_buf_t *buf = dev->transfer.bufs[subd->idx];
	int tmp;

	/* Basic checking */
	if (!test_bit(A4L_TSF_BUSY, &(dev->transfer.status[subd->idx])))
		return -ENOENT;

	/* Even if it is a little more complex,
	   atomic operations are used so as 
	   to prevent any kind of corner case */
	while ((tmp = ffs(evts) - 1) != -1) {
		set_bit(tmp, &buf->evt_flags);
		clear_bit(tmp, &evts);
	}

	/* Notify the user-space side */
	a4l_signal_sync(&buf->sync);

	return 0;
}

unsigned long a4l_buf_count(a4l_subd_t *subd)
{
	unsigned long ret = 0;
	a4l_dev_t *dev = subd->dev;
	a4l_buf_t *buf = dev->transfer.bufs[subd->idx];

	if (subd->flags & A4L_SUBD_MASK_READ)
		ret = __count_to_put(buf);
	else if (subd->flags & A4L_SUBD_MASK_WRITE)
		ret = __count_to_get(buf);	

	return ret;
}

/* --- Mmap functions --- */

void a4l_map(struct vm_area_struct *area)
{
	unsigned long *status = (unsigned long *)area->vm_private_data;
	set_bit(A4L_TSF_MMAP, status);
}

void a4l_unmap(struct vm_area_struct *area)
{
	unsigned long *status = (unsigned long *)area->vm_private_data;
	clear_bit(A4L_TSF_MMAP, status);
}

static struct vm_operations_struct a4l_vm_ops = {
open:a4l_map,
close:a4l_unmap,
};

int a4l_ioctl_mmap(a4l_cxt_t *cxt, void *arg)
{
	a4l_mmap_t map_cfg;
	a4l_dev_t *dev;
	int ret;

	__a4l_dbg(1, core_dbg, 
		  "a4l_ioctl_mmap: minor=%d\n", a4l_get_minor(cxt));

	/* The mmap operation cannot be performed in a 
	   real-time context */
	if (rtdm_in_rt_context()) {
		return -ENOSYS;
	}

	dev = a4l_get_dev(cxt);

	/* Basically check the device */
	if (!test_bit(A4L_DEV_ATTACHED, &dev->flags)) {
		__a4l_err("a4l_ioctl_mmap: cannot mmap on "
			  "an unattached device\n");
		return -EINVAL;
	}

	/* Recover the argument structure */
	if (rtdm_safe_copy_from_user(cxt->user_info,
				     &map_cfg, arg, sizeof(a4l_mmap_t)) != 0)
		return -EFAULT;

	/* Check the subdevice */
	if (map_cfg.idx_subd >= dev->transfer.nb_subd) {
		__a4l_err("a4l_ioctl_mmap: subdevice index "
			  "out of range (idx=%d)\n", map_cfg.idx_subd);
		return -EINVAL;
	}

	if ((dev->transfer.subds[map_cfg.idx_subd]->flags & A4L_SUBD_CMD) == 0) {
		__a4l_err("a4l_ioctl_mmap: operation not supported, "
			  "synchronous only subdevice\n");
		return -EINVAL;
	}

	if ((dev->transfer.subds[map_cfg.idx_subd]->flags & A4L_SUBD_MMAP) == 0) {
		__a4l_err("a4l_ioctl_mmap: mmap not allowed on this subdevice\n");
		return -EINVAL;
	}

	/* Check the buffer is not already mapped */
	if (test_bit(A4L_TSF_MMAP,
		     &(dev->transfer.status[map_cfg.idx_subd]))) {
		__a4l_err("a4l_ioctl_mmap: mmap is already done\n");
		return -EBUSY;
	}

	/* Basically check the size to be mapped */
	if ((map_cfg.size & ~(PAGE_MASK)) != 0 ||
	    map_cfg.size > dev->transfer.bufs[map_cfg.idx_subd]->size)
		return -EFAULT;

	ret = rtdm_mmap_to_user(cxt->user_info,
				dev->transfer.bufs[map_cfg.idx_subd]->buf,
				map_cfg.size,
				PROT_READ | PROT_WRITE,
				&map_cfg.ptr,
				&a4l_vm_ops,
				&(dev->transfer.status[map_cfg.idx_subd]));

	if (ret < 0) {
		__a4l_err("a4l_ioctl_mmap: internal error, "
			  "rtdm_mmap_to_user failed (err=%d)\n", ret);
		return ret;
	}

	return rtdm_safe_copy_to_user(cxt->user_info, 
				      arg, &map_cfg, sizeof(a4l_mmap_t));
}

/* --- IOCTL / FOPS functions --- */

int a4l_ioctl_bufcfg(a4l_cxt_t * cxt, void *arg)
{
	a4l_dev_t *dev = a4l_get_dev(cxt);
	a4l_bufcfg_t buf_cfg;

	__a4l_dbg(1, core_dbg, 
		  "a4l_ioctl_bufcfg: minor=%d\n", a4l_get_minor(cxt));

	/* As Linux API is used to allocate a virtual buffer,
	   the calling process must not be in primary mode */
	if (rtdm_in_rt_context()) {
		return -ENOSYS;
	}

	/* Basic checking */
	if (!test_bit(A4L_DEV_ATTACHED, &dev->flags)) {
		__a4l_err("a4l_ioctl_bufcfg: unattached device\n");
		return -EINVAL;
	}

	if (rtdm_safe_copy_from_user(cxt->user_info,
				     &buf_cfg, 
				     arg, sizeof(a4l_bufcfg_t)) != 0)
		return -EFAULT;

	/* Check the subdevice */
	if (buf_cfg.idx_subd >= dev->transfer.nb_subd) {
		__a4l_err("a4l_ioctl_bufcfg: subdevice index "
			  "out of range (idx=%d)\n", buf_cfg.idx_subd);
		return -EINVAL;
	}

	if ((dev->transfer.subds[buf_cfg.idx_subd]->flags & A4L_SUBD_CMD) == 0) {
		__a4l_err("a4l_ioctl_bufcfg: operation not supported, "
			  "synchronous only subdevice\n");
		return -EINVAL;
	}

	if (buf_cfg.buf_size > A4L_BUF_MAXSIZE) {
		__a4l_err("a4l_ioctl_bufcfg: buffer size too big (<=16MB)\n");
		return -EINVAL;
	}

	/* If a transfer is occuring or if the buffer is mmapped,
	   no buffer size change is allowed */
	if (test_bit(A4L_TSF_BUSY,
		     &(dev->transfer.status[buf_cfg.idx_subd]))) {
		__a4l_err("a4l_ioctl_bufcfg: acquisition in progress\n");
		return -EBUSY;
	}

	if (test_bit(A4L_TSF_MMAP,
		     &(dev->transfer.status[buf_cfg.idx_subd]))) {
		__a4l_err("a4l_ioctl_bufcfg: please unmap before "
			  "configuring buffer\n");
		return -EPERM;
	}

	/* Performs the re-allocation */
	a4l_free_buffer(dev->transfer.bufs[buf_cfg.idx_subd]);

	dev->transfer.bufs[buf_cfg.idx_subd]->size = buf_cfg.buf_size;

	return a4l_alloc_buffer(dev->transfer.bufs[buf_cfg.idx_subd]);
}

int a4l_ioctl_bufinfo(a4l_cxt_t * cxt, void *arg)
{
	a4l_dev_t *dev = a4l_get_dev(cxt);
	a4l_bufinfo_t info;
	a4l_buf_t *buf;
	unsigned long tmp_cnt;
	int ret;

	__a4l_dbg(1, core_dbg, 
		  "a4l_ioctl_bufinfo: minor=%d\n", a4l_get_minor(cxt));

	if (!rtdm_in_rt_context() && rtdm_rt_capable(cxt->user_info))
		return -ENOSYS;

	/* Basic checking */
	if (!test_bit(A4L_DEV_ATTACHED, &dev->flags)) {
		__a4l_err("a4l_ioctl_bufinfo: unattached device\n");
		return -EINVAL;
	}

	if (rtdm_safe_copy_from_user(cxt->user_info,
				     &info, arg, sizeof(a4l_bufinfo_t)) != 0)
		return -EFAULT;

	/* Check the subdevice */
	if (info.idx_subd >= dev->transfer.nb_subd) {
		__a4l_err("a4l_ioctl_bufinfo: subdevice index "
			  "out of range (idx=%d)\n", info.idx_subd);
		return -EINVAL;
	}

	if ((dev->transfer.subds[info.idx_subd]->flags & A4L_SUBD_CMD) == 0) {
		__a4l_err("a4l_ioctl_bufinfo: operation not supported, "
			  "synchronous only subdevice\n");
		return -EINVAL;
	}

	buf = dev->transfer.bufs[info.idx_subd];

	/* If a transfer is not occuring, simply return buffer
	   informations, otherwise make the transfer progress */
	if (!test_bit(A4L_TSF_BUSY,
		       &(dev->transfer.status[info.idx_subd]))) {
		info.rw_count = 0;
		goto a4l_ioctl_bufinfo_out;
	}

	ret = __handle_event(buf);

	if (info.idx_subd == dev->transfer.idx_read_subd) {

		/* Updates consume count if rw_count is not null */
		if (info.rw_count != 0)
			buf->cns_count += info.rw_count;

		/* Retrieves the data amount to read */
		tmp_cnt = info.rw_count = __count_to_get(buf);

		__a4l_dbg(1, core_dbg, 
			  "a4l_ioctl_bufinfo: count to read=%lu\n", tmp_cnt);

		if ((ret < 0 && ret != -ENOENT) ||
		    (ret == -ENOENT && tmp_cnt == 0)) {
			a4l_cancel_transfer(cxt, info.idx_subd);
			return ret;
		}
	} else if (info.idx_subd == dev->transfer.idx_write_subd) {

		if (ret < 0) {
			a4l_cancel_transfer(cxt, info.idx_subd);
			if (info.rw_count != 0)
				return ret;
		}

		/* If rw_count is not null, 
		   there is something to write / munge  */
		if (info.rw_count != 0 && info.rw_count <= __count_to_put(buf)) {

			/* Updates the production pointer */
			buf->prd_count += info.rw_count;

			/* Sets the munge count */
			tmp_cnt = info.rw_count;
		} else
			tmp_cnt = 0;

		/* Retrieves the data amount which is writable */
		info.rw_count = __count_to_put(buf);

		__a4l_dbg(1, core_dbg, 
			  "a4l_ioctl_bufinfo: count to write=%lu\n", 
			  info.rw_count);

	} else {
		__a4l_err("a4l_ioctl_bufinfo: wrong subdevice selected\n");
		return -EINVAL;
	}

	/* Performs the munge if need be */
	if (dev->transfer.subds[info.idx_subd]->munge != NULL) {
		__munge(dev->transfer.subds[info.idx_subd],
			dev->transfer.subds[info.idx_subd]->munge,
			buf, tmp_cnt);

		/* Updates munge count */
		buf->mng_count += tmp_cnt;
	}

a4l_ioctl_bufinfo_out:	

	/* Sets the buffer size */
	info.buf_size = buf->size;

	/* Sends the structure back to user space */
	if (rtdm_safe_copy_to_user(cxt->user_info,
				   arg, &info, sizeof(a4l_bufinfo_t)) != 0)
		return -EFAULT;

	return 0;
}

ssize_t a4l_read(a4l_cxt_t * cxt, void *bufdata, size_t nbytes)
{
	a4l_dev_t *dev = a4l_get_dev(cxt);
	int idx_subd = dev->transfer.idx_read_subd;
	a4l_buf_t *buf = dev->transfer.bufs[idx_subd];
	ssize_t count = 0;

	/* Basic checkings */
	if (!test_bit(A4L_DEV_ATTACHED, &dev->flags)) {
		__a4l_err("a4l_read: unattached device\n");
		return -EINVAL;
	}

	if (!test_bit(A4L_TSF_BUSY, &(dev->transfer.status[idx_subd]))) {
		__a4l_err("a4l_read: idle subdevice\n");
		return -ENOENT;
	}

	/* TODO: to be removed
	   Check the subdevice capabilities */
	if ((dev->transfer.subds[idx_subd]->flags & A4L_SUBD_CMD) == 0) {	       
		__a4l_err("a4l_read: incoherent state\n");
		return -EINVAL;
	}

	while (count < nbytes) {

		/* Check the events */
		int ret = __handle_event(buf);

		/* Compute the data amount to copy */
		unsigned long tmp_cnt = __count_to_get(buf);

		/* Check tmp_cnt count is not higher than
		   the global count to read */
		if (tmp_cnt > nbytes - count)
			tmp_cnt = nbytes - count;

		/* We check whether there is an error */
		if (ret < 0 && ret != -ENOENT) {
			a4l_cancel_transfer(cxt, idx_subd);
			count = ret;
			goto out_a4l_read;			
		}
		
		/* We check whether the acquisition is over */
		if (ret == -ENOENT && tmp_cnt == 0) {
			a4l_cancel_transfer(cxt, idx_subd);
			count = 0;
			goto out_a4l_read;
		}

		if (tmp_cnt > 0) {

			/* Performs the munge if need be */
			if (dev->transfer.subds[idx_subd]->munge != NULL) {
				__munge(dev->transfer.subds[idx_subd],
					dev->transfer.subds[idx_subd]->munge,
					buf, tmp_cnt);

				/* Updates munge count */
				buf->mng_count += tmp_cnt;
			}

			/* Performs the copy */
			ret = __consume(cxt, buf, bufdata + count, tmp_cnt);

			if (ret < 0) {
				count = ret;
				goto out_a4l_read;
			}

			/* Updates consume count */
			buf->cns_count += tmp_cnt;

			/* Updates the return value */
			count += tmp_cnt;

			/* If the driver does not work in bulk mode,
			   we must leave this function */
			if (!test_bit(A4L_TSF_BULK,
				      &(dev->transfer.status[idx_subd])))
				goto out_a4l_read;
		}
		/* If the acquisition is not over, we must not
		   leave the function without having read a least byte */
		else {
			ret = a4l_wait_sync(&(buf->sync), rtdm_in_rt_context());
			if (ret < 0) {
				if (ret == -ERESTARTSYS)
					ret = -EINTR;
				count = ret;
				goto out_a4l_read;
			}
		}
	}

out_a4l_read:

	return count;
}

ssize_t a4l_write(a4l_cxt_t *cxt, 
		  const void *bufdata, size_t nbytes)
{
	a4l_dev_t *dev = a4l_get_dev(cxt);
	int idx_subd = dev->transfer.idx_write_subd;
	a4l_buf_t *buf = dev->transfer.bufs[idx_subd];
	ssize_t count = 0;

	/* Basic checkings */
	if (!test_bit(A4L_DEV_ATTACHED, &dev->flags)) {
		__a4l_err("a4l_write: unattached device\n");
		return -EINVAL;
	}

	if (!test_bit(A4L_TSF_BUSY, &(dev->transfer.status[idx_subd]))) {
		__a4l_err("a4l_write: idle subdevice\n");
		return -ENOENT;
	}

	/* TODO: to be removed
	   Check the subdevice capabilities */
	if ((dev->transfer.subds[idx_subd]->flags & A4L_SUBD_CMD) == 0) {       
		__a4l_err("a4l_write: incoherent state\n");
		return -EINVAL;
	}

	while (count < nbytes) {

		/* Check the events */
		int ret = __handle_event(buf);

		/* Compute the data amount to copy */
		unsigned long tmp_cnt = __count_to_put(buf);

		/* Check tmp_cnt count is not higher than
		   the global count to write */
		if (tmp_cnt > nbytes - count)
			tmp_cnt = nbytes - count;

		if (ret < 0) {
			a4l_cancel_transfer(cxt, idx_subd);
			count = (ret == -ENOENT) ? -EINVAL : ret;
			goto out_a4l_write;
		}

		if (tmp_cnt > 0) {

			/* Performs the copy */
			ret = __produce(cxt, 
					buf, (void *)bufdata + count, tmp_cnt);
			if (ret < 0) {
				count = ret;
				goto out_a4l_write;
			}

			/* Performs the munge if need be */
			if (dev->transfer.subds[idx_subd]->munge != NULL) {
				__munge(dev->transfer.subds[idx_subd],
					dev->transfer.subds[idx_subd]->munge,
					buf, tmp_cnt);

				/* Updates munge count */
				buf->mng_count += tmp_cnt;
			}

			/* Updates produce count */
			buf->prd_count += tmp_cnt;

			/* Updates the return value */
			count += tmp_cnt;

			/* If the driver does not work in bulk mode,
			   we must leave this function */
			if (!test_bit(A4L_TSF_BULK,
				      &(dev->transfer.status[idx_subd])))
				goto out_a4l_write;
		} else {
			/* The buffer is full, we have to wait for a slot to free */
			ret = a4l_wait_sync(&(buf->sync), rtdm_in_rt_context());
			if (ret < 0) {
				if (ret == -ERESTARTSYS)
					ret = -EINTR;
				count = ret;
				goto out_a4l_write;
			}
		}
	}

out_a4l_write:

	return count;
}

int a4l_select(a4l_cxt_t *cxt, 
	       rtdm_selector_t *selector,
	       enum rtdm_selecttype type, unsigned fd_index)
{
	a4l_dev_t *dev = a4l_get_dev(cxt);
	int idx_subd = (type == RTDM_SELECTTYPE_READ) ? 
		dev->transfer.idx_read_subd :
		dev->transfer.idx_write_subd;
	a4l_buf_t *buf = dev->transfer.bufs[idx_subd];

	/* Check the RTDM select type 
	   (RTDM_SELECTTYPE_EXCEPT is not supported) */
	if(type != RTDM_SELECTTYPE_READ && 
	   type != RTDM_SELECTTYPE_WRITE) {
		__a4l_err("a4l_select: wrong select argument\n");
		return -EINVAL;
	}

	/* Basic checkings */
	if (!test_bit(A4L_DEV_ATTACHED, &dev->flags)) {
		__a4l_err("a4l_select: unattached device\n");
		return -EINVAL;
	}

	if (!test_bit(A4L_TSF_BUSY, &(dev->transfer.status[idx_subd]))) {
		__a4l_err("a4l_select: idle subdevice\n");
		return -ENOENT;	
	}

	/* Performs a bind on the Analogy synchronization element */
	return a4l_select_sync(&(buf->sync), selector, type, fd_index);	
}

int a4l_ioctl_poll(a4l_cxt_t * cxt, void *arg)
{
	int ret = 0;
	unsigned long tmp_cnt = 0;
	a4l_dev_t *dev = a4l_get_dev(cxt);
	a4l_buf_t *buf;
	a4l_poll_t poll;

	if (!rtdm_in_rt_context() && rtdm_rt_capable(cxt->user_info))
		return -ENOSYS;

	/* Basic checking */
	if (!test_bit(A4L_DEV_ATTACHED, &dev->flags)) {
		__a4l_err("a4l_poll: unattached device\n");
		return -EINVAL;
	}

	if (rtdm_safe_copy_from_user(cxt->user_info, 
				     &poll, arg, sizeof(a4l_poll_t)) != 0)
		return -EFAULT;

	/* Check the subdevice */
	if (poll.idx_subd >= dev->transfer.nb_subd) {
		__a4l_err("a4l_poll: subdevice index out of range (idx=%d)\n", 
			  poll.idx_subd);
		return -EINVAL;
	}

	if ((dev->transfer.subds[poll.idx_subd]->flags & A4L_SUBD_CMD) == 0) {
		__a4l_err("a4l_poll: operation not supported, "
			  "synchronous only subdevice\n");
		return -EINVAL;
	}

	if ((dev->transfer.subds[poll.idx_subd]->flags & 
	     A4L_SUBD_MASK_SPECIAL) != 0) {
		__a4l_err("a4l_poll: wrong subdevice selected\n");
		return -EINVAL;
	}

	/* Checks a transfer is occuring */
	if (!test_bit(A4L_TSF_BUSY, &(dev->transfer.status[poll.idx_subd]))) {
		__a4l_err("a4l_poll: idle subdevice\n");
		return -EINVAL;
	}

	buf = dev->transfer.bufs[poll.idx_subd];
	
	/* Checks the buffer events */
	a4l_flush_sync(&buf->sync);
	ret = __handle_event(buf);

	/* Retrieves the data amount to compute 
	   according to the subdevice type */
	if ((dev->transfer.subds[poll.idx_subd]->flags &
	     A4L_SUBD_MASK_READ) != 0) {

		tmp_cnt = __count_to_get(buf);

		/* Check if some error occured */
		if (ret < 0 && ret != -ENOENT) {
			a4l_cancel_transfer(cxt, poll.idx_subd);
			return ret;
		}

		/* Check whether the acquisition is over */
		if (ret == -ENOENT && tmp_cnt == 0) {
			a4l_cancel_transfer(cxt, poll.idx_subd);
			return 0;
		}
	} else {

		/* If some error was detected, cancel the transfer */
		if (ret < 0) {
			a4l_cancel_transfer(cxt, poll.idx_subd);
			return ret;
		}

		tmp_cnt = __count_to_put(buf);
	}

	if (poll.arg == A4L_NONBLOCK || tmp_cnt != 0)
		goto out_poll;

	if (poll.arg == A4L_INFINITE)
		ret = a4l_wait_sync(&(buf->sync), rtdm_in_rt_context());
	else {
		unsigned long long ns = ((unsigned long long)poll.arg) *
			((unsigned long long)NSEC_PER_MSEC);
		ret = a4l_timedwait_sync(&(buf->sync), 
					 rtdm_in_rt_context(), ns);
	}

	if (ret == 0) {
		/* Retrieves the count once more */
		if ((dev->transfer.subds[poll.idx_subd]->flags &
		     A4L_SUBD_MASK_READ) != 0)
			tmp_cnt = __count_to_get(buf);
		else
			tmp_cnt = __count_to_put(buf);
	}

out_poll:

	poll.arg = tmp_cnt;

	/* Sends the structure back to user space */
	ret = rtdm_safe_copy_to_user(cxt->user_info, 
				     arg, &poll, sizeof(a4l_poll_t));

	return ret;
}

#endif /* !DOXYGEN_CPP */

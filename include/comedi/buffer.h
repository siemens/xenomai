/**
 * @file
 * Comedi for RTDM, buffer related features
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

#ifndef __COMEDI_BUFFER_H__
#define __COMEDI_BUFFER_H__

#ifndef DOXYGEN_CPP

#ifdef __KERNEL__

#include <linux/version.h>
#include <linux/mm.h>

#include <rtdm/rtdm_driver.h>

#include <comedi/context.h>

/* Buffer copies directions */
#define COMEDI_BUF_PUT 1
#define COMEDI_BUF_GET 2

/* Events bits */
#define COMEDI_BUF_EOBUF_NR 0
#define COMEDI_BUF_ERROR_NR 1
#define COMEDI_BUF_EOA_NR 2
/* Events flags */
#define COMEDI_BUF_EOBUF (1 << COMEDI_BUF_EOBUF_NR)
#define COMEDI_BUF_ERROR (1 << COMEDI_BUF_ERROR_NR)
#define COMEDI_BUF_EOA (1 << COMEDI_BUF_EOA_NR)

struct comedi_device;

/* Buffer descriptor structure */
struct comedi_buffer {

	/* Buffer's first virtual page pointer */
	void *buf;

	/* Buffer's global size */
	unsigned long size;
	/* Tab containing buffer's pages pointers */
	unsigned long *pg_list;

	/* RT/NRT synchronization element */
	comedi_sync_t sync;

	/* Counters needed for transfer */
	unsigned long end_count;
	unsigned long prd_count;
	unsigned long cns_count;
	unsigned long tmp_count;

	/* Events occuring during transfer */
	unsigned long evt_flags;

	/* Command on progress */
	comedi_cmd_t *cur_cmd;

	/* Munge counter */
	unsigned long mng_count;
};
typedef struct comedi_buffer comedi_buf_t;

/* Static inline Buffer related functions */

/* Produce memcpy function */
static inline int __produce(comedi_cxt_t * cxt,
			    comedi_buf_t * buf, void *pin, unsigned long count)
{
	unsigned long start_ptr = (buf->prd_count % buf->size);
	unsigned long tmp_cnt = count;
	int ret = 0;

	while (ret == 0 && tmp_cnt != 0) {
		/* Checks the data copy can be performed contiguously */
		unsigned long blk_size = (start_ptr + tmp_cnt > buf->size) ?
		    buf->size - start_ptr : tmp_cnt;

		/* Performs the copy */
		if (cxt == NULL)
			memcpy(buf->buf + start_ptr, pin, blk_size);
		else
			ret = comedi_copy_from_user(cxt,
						    buf->buf + start_ptr,
						    pin, blk_size);

		/* Updates pointers/counts */
		pin += blk_size;
		tmp_cnt -= blk_size;
		start_ptr = 0;
	}

	return ret;
}

/* Consume memcpy function */
static inline int __consume(comedi_cxt_t * cxt,
			    comedi_buf_t * buf, void *pout, unsigned long count)
{
	unsigned long start_ptr = (buf->cns_count % buf->size);
	unsigned long tmp_cnt = count;
	int ret = 0;

	while (ret == 0 && tmp_cnt != 0) {
		/* Checks the data copy can be performed contiguously */
		unsigned long blk_size = (start_ptr + tmp_cnt > buf->size) ?
		    buf->size - start_ptr : tmp_cnt;

		/* Performs the copy */
		if (cxt == NULL)
			memcpy(pout, buf->buf + start_ptr, blk_size);
		else
			ret = comedi_copy_to_user(cxt,
						  pout,
						  buf->buf + start_ptr,
						  blk_size);

		/* Updates pointers/counts */
		pout += blk_size;
		tmp_cnt -= blk_size;
		start_ptr = 0;
	}

	return ret;
}

/* Munge procedure */
static inline void __munge(comedi_cxt_t * cxt,
			   void (*munge) (comedi_cxt_t *, int, void *,
					  unsigned long), int idx_subd,
			   comedi_buf_t * buf, unsigned long count)
{
	unsigned long start_ptr = (buf->mng_count % buf->size);
	unsigned long tmp_cnt = count;

	while (tmp_cnt != 0) {
		/* Checks the data copy can be performed contiguously */
		unsigned long blk_size = (start_ptr + tmp_cnt > buf->size) ?
		    buf->size - start_ptr : tmp_cnt;

		/* Performs the munge operation */
		munge(cxt, idx_subd, buf->buf + start_ptr, blk_size);

		/* Updates the start pointer and the count */
		tmp_cnt -= blk_size;
		start_ptr = 0;
	}
}

/* Event consumption function */
static inline int __handle_event(comedi_buf_t * buf)
{
	int ret = 0;

	/* The event "End of acquisition" must not be cleaned
	   before the complete flush of the buffer */
	if (test_bit(COMEDI_BUF_EOA_NR, &buf->evt_flags)) {
		ret = -ENOENT;
	}

	if (test_bit(COMEDI_BUF_ERROR_NR, &buf->evt_flags)) {
		ret = -EPIPE;
	}

	return ret;
}

/* Counters management functions */

static inline int __pre_abs_put(comedi_buf_t * buf, unsigned long count)
{
	if (count - buf->tmp_count > buf->size) {
		set_bit(COMEDI_BUF_ERROR_NR, &buf->evt_flags);
		return -EPIPE;
	}

	buf->tmp_count = buf->cns_count;

	return 0;
}

static inline int __pre_put(comedi_buf_t * buf, unsigned long count)
{
	return __pre_abs_put(buf, buf->tmp_count + count);
}

static inline int __pre_abs_get(comedi_buf_t * buf, unsigned long count)
{
	if (!(buf->tmp_count == 0 && buf->cns_count == 0) &&
	    (long)(count - buf->tmp_count) > 0) {
		set_bit(COMEDI_BUF_ERROR_NR, &buf->evt_flags);
		return -EPIPE;
	}

	buf->tmp_count = buf->prd_count;

	return 0;
}

static inline int __pre_get(comedi_buf_t * buf, unsigned long count)
{
	return __pre_abs_get(buf, buf->tmp_count + count);
}

static inline int __abs_put(comedi_buf_t * buf, unsigned long count)
{
	unsigned long old = buf->prd_count;

	if (buf->prd_count >= count)
		return -EINVAL;

	buf->prd_count = count;

	if ((old / buf->size) != (count / buf->size))
		set_bit(COMEDI_BUF_EOBUF_NR, &buf->evt_flags);

	if (count >= buf->end_count)
		set_bit(COMEDI_BUF_EOA_NR, &buf->evt_flags);

	return 0;
}

static inline int __put(comedi_buf_t * buf, unsigned long count)
{
	return __abs_put(buf, buf->prd_count + count);
}

static inline int __abs_get(comedi_buf_t * buf, unsigned long count)
{
	unsigned long old = buf->cns_count;

	if (buf->cns_count >= count)
		return -EINVAL;

	buf->cns_count = count;

	if ((old / buf->size) != count / buf->size)
		set_bit(COMEDI_BUF_EOBUF_NR, &buf->evt_flags);

	if (count >= buf->end_count)
		set_bit(COMEDI_BUF_EOA_NR, &buf->evt_flags);

	return 0;
}

static inline int __get(comedi_buf_t * buf, unsigned long count)
{
	return __abs_get(buf, buf->cns_count + count);
}

static inline unsigned long __count_to_put(comedi_buf_t * buf)
{
	unsigned long ret;

	if (buf->size + buf->cns_count > buf->prd_count)
		ret = buf->size + buf->cns_count - buf->prd_count;
	else
		ret = 0;

	return ret;
}

static inline unsigned long __count_to_get(comedi_buf_t * buf)
{
	unsigned long ret;

	if (buf->end_count != 0 && (buf->end_count > buf->prd_count))
		ret = buf->prd_count;
	else
		ret = buf->end_count;

	if (ret > buf->cns_count)
		ret -= buf->cns_count;
	else
		ret = 0;

	return ret;
}

/* --- Buffer internal functions --- */

int comedi_alloc_buffer(comedi_buf_t * buf_desc);

void comedi_free_buffer(comedi_buf_t * buf_desc);

int comedi_buf_prepare_absput(struct comedi_device *dev, unsigned long count);

int comedi_buf_commit_absput(struct comedi_device *dev, unsigned long count);

int comedi_buf_prepare_put(struct comedi_device *dev, unsigned long count);

int comedi_buf_commit_put(struct comedi_device *dev, unsigned long count);

int comedi_buf_put(struct comedi_device *dev,
		   void *bufdata, unsigned long count);

int comedi_buf_prepare_absget(struct comedi_device *dev, unsigned long count);

int comedi_buf_commit_absget(struct comedi_device *dev, unsigned long count);

int comedi_buf_prepare_get(struct comedi_device *dev, unsigned long count);

int comedi_buf_commit_get(struct comedi_device *dev, unsigned long count);

int comedi_buf_get(struct comedi_device *dev,
		   void *bufdata, unsigned long count);

int comedi_buf_evt(struct comedi_device *dev,
		   unsigned int type, unsigned long evts);

unsigned long comedi_buf_count(struct comedi_device *dev, unsigned int type);

/* --- Current Command management function --- */

comedi_cmd_t *comedi_get_cmd(struct comedi_device *dev, unsigned int type,
			     int idx_subd);

/* --- Munge related function --- */

int comedi_get_chan(struct comedi_device *dev, unsigned int type, int idx_subd);

/* --- IOCTL / FOPS functions --- */

int comedi_ioctl_mmap(comedi_cxt_t * cxt, void *arg);
int comedi_ioctl_bufcfg(comedi_cxt_t * cxt, void *arg);
int comedi_ioctl_bufinfo(comedi_cxt_t * cxt, void *arg);
int comedi_ioctl_poll(comedi_cxt_t * cxt, void *arg);
ssize_t comedi_read(comedi_cxt_t * cxt, void *bufdata, size_t nbytes);
ssize_t comedi_write(comedi_cxt_t * cxt, 
		     const void *bufdata, size_t nbytes);
int comedi_select(comedi_cxt_t *cxt, 
		  rtdm_selector_t *selector,
		  enum rtdm_selecttype type, unsigned fd_index);

#endif /* __KERNEL__ */

/* MMAP ioctl argument structure */
struct comedi_mmap_arg {
	unsigned int idx_subd;
	unsigned long size;
	void *ptr;
};
typedef struct comedi_mmap_arg comedi_mmap_t;

/* Constants related with buffer size
   (might be used with BUFCFG ioctl) */
#define COMEDI_BUF_MAXSIZE 0x1000000
#define COMEDI_BUF_DEFSIZE 0x10000

/* BUFCFG ioctl argument structure */
struct comedi_buffer_config {
	unsigned int idx_subd;
	unsigned long buf_size;
};
typedef struct comedi_buffer_config comedi_bufcfg_t;

/* BUFINFO ioctl argument structure */
struct comedi_buffer_info {
	unsigned int idx_subd;
	unsigned long buf_size;
	unsigned long rw_count;
};
typedef struct comedi_buffer_info comedi_bufinfo_t;

/* POLL ioctl argument structure */
struct comedi_poll {
	unsigned int idx_subd;
	unsigned long arg;
};
typedef struct comedi_poll comedi_poll_t;

#endif /* !DOXYGEN_CPP */

#endif /* __COMEDI_BUFFER_H__ */

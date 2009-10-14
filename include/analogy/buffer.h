/**
 * @file
 * Analogy for Linux, buffer related features
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

#ifndef __ANALOGY_BUFFER_H__
#define __ANALOGY_BUFFER_H__

#ifndef DOXYGEN_CPP

#ifdef __KERNEL__

#include <linux/version.h>
#include <linux/mm.h>

#include <rtdm/rtdm_driver.h>

#include <analogy/context.h>

/* Events bits */
#define A4L_BUF_EOBUF_NR 0
#define A4L_BUF_ERROR_NR 1
#define A4L_BUF_EOA_NR 2
/* Events flags */
#define A4L_BUF_EOBUF (1 << A4L_BUF_EOBUF_NR)
#define A4L_BUF_ERROR (1 << A4L_BUF_ERROR_NR)
#define A4L_BUF_EOA (1 << A4L_BUF_EOA_NR)

struct a4l_subdevice;

/* Buffer descriptor structure */
struct a4l_buffer {

	/* Buffer's first virtual page pointer */
	void *buf;

	/* Buffer's global size */
	unsigned long size;
	/* Tab containing buffer's pages pointers */
	unsigned long *pg_list;

	/* RT/NRT synchronization element */
	a4l_sync_t sync;

	/* Counters needed for transfer */
	unsigned long end_count;
	unsigned long prd_count;
	unsigned long cns_count;
	unsigned long tmp_count;

	/* Events occuring during transfer */
	unsigned long evt_flags;

	/* Command on progress */
	a4l_cmd_t *cur_cmd;

	/* Munge counter */
	unsigned long mng_count;
};
typedef struct a4l_buffer a4l_buf_t;

/* Static inline Buffer related functions */

/* Produce memcpy function */
static inline int __produce(a4l_cxt_t * cxt,
			    a4l_buf_t * buf, void *pin, unsigned long count)
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
			ret = rtdm_safe_copy_from_user(cxt->user_info,
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
static inline int __consume(a4l_cxt_t * cxt,
			    a4l_buf_t * buf, void *pout, unsigned long count)
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
			ret = rtdm_safe_copy_to_user(cxt->user_info,
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
static inline void __munge(struct a4l_subdevice * subd,
			   void (*munge) (struct a4l_subdevice *, 
					  void *, unsigned long),
			   a4l_buf_t * buf, unsigned long count)
{
	unsigned long start_ptr = (buf->mng_count % buf->size);
	unsigned long tmp_cnt = count;

	while (tmp_cnt != 0) {
		/* Checks the data copy can be performed contiguously */
		unsigned long blk_size = (start_ptr + tmp_cnt > buf->size) ?
			buf->size - start_ptr : tmp_cnt;

		/* Performs the munge operation */
		munge(subd, buf->buf + start_ptr, blk_size);

		/* Updates the start pointer and the count */
		tmp_cnt -= blk_size;
		start_ptr = 0;
	}
}

/* Event consumption function */
static inline int __handle_event(a4l_buf_t * buf)
{
	int ret = 0;

	/* The event "End of acquisition" must not be cleaned
	   before the complete flush of the buffer */
	if (test_bit(A4L_BUF_EOA_NR, &buf->evt_flags)) {
		ret = -ENOENT;
	}

	if (test_bit(A4L_BUF_ERROR_NR, &buf->evt_flags)) {
		ret = -EPIPE;
	}

	return ret;
}

/* Counters management functions */

static inline int __pre_abs_put(a4l_buf_t * buf, unsigned long count)
{
	if (count - buf->tmp_count > buf->size) {
		set_bit(A4L_BUF_ERROR_NR, &buf->evt_flags);
		return -EPIPE;
	}

	buf->tmp_count = buf->cns_count;

	return 0;
}

static inline int __pre_put(a4l_buf_t * buf, unsigned long count)
{
	return __pre_abs_put(buf, buf->tmp_count + count);
}

static inline int __pre_abs_get(a4l_buf_t * buf, unsigned long count)
{
	if (!(buf->tmp_count == 0 && buf->cns_count == 0) &&
	    (long)(count - buf->tmp_count) > 0) {
		set_bit(A4L_BUF_ERROR_NR, &buf->evt_flags);
		return -EPIPE;
	}

	buf->tmp_count = buf->prd_count;

	return 0;
}

static inline int __pre_get(a4l_buf_t * buf, unsigned long count)
{
	return __pre_abs_get(buf, buf->tmp_count + count);
}

static inline int __abs_put(a4l_buf_t * buf, unsigned long count)
{
	unsigned long old = buf->prd_count;

	if (buf->prd_count >= count)
		return -EINVAL;

	buf->prd_count = count;

	if ((old / buf->size) != (count / buf->size))
		set_bit(A4L_BUF_EOBUF_NR, &buf->evt_flags);

	if (count >= buf->end_count)
		set_bit(A4L_BUF_EOA_NR, &buf->evt_flags);

	return 0;
}

static inline int __put(a4l_buf_t * buf, unsigned long count)
{
	return __abs_put(buf, buf->prd_count + count);
}

static inline int __abs_get(a4l_buf_t * buf, unsigned long count)
{
	unsigned long old = buf->cns_count;

	if (buf->cns_count >= count)
		return -EINVAL;

	buf->cns_count = count;

	if ((old / buf->size) != count / buf->size)
		set_bit(A4L_BUF_EOBUF_NR, &buf->evt_flags);

	if (count >= buf->end_count)
		set_bit(A4L_BUF_EOA_NR, &buf->evt_flags);

	return 0;
}

static inline int __get(a4l_buf_t * buf, unsigned long count)
{
	return __abs_get(buf, buf->cns_count + count);
}

static inline unsigned long __count_to_put(a4l_buf_t * buf)
{
	unsigned long ret;

	if (buf->size + buf->cns_count > buf->prd_count)
		ret = buf->size + buf->cns_count - buf->prd_count;
	else
		ret = 0;

	return ret;
}

static inline unsigned long __count_to_get(a4l_buf_t * buf)
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

int a4l_alloc_buffer(a4l_buf_t * buf_desc);

void a4l_free_buffer(a4l_buf_t * buf_desc);

int a4l_buf_prepare_absput(struct a4l_subdevice *subd, 
			   unsigned long count);

int a4l_buf_commit_absput(struct a4l_subdevice *subd, 
			  unsigned long count);

int a4l_buf_prepare_put(struct a4l_subdevice *subd, 
			unsigned long count);

int a4l_buf_commit_put(struct a4l_subdevice *subd, 
		       unsigned long count);

int a4l_buf_put(struct a4l_subdevice *subd,
		void *bufdata, unsigned long count);

int a4l_buf_prepare_absget(struct a4l_subdevice *subd, 
			   unsigned long count);

int a4l_buf_commit_absget(struct a4l_subdevice *subd, 
			  unsigned long count);

int a4l_buf_prepare_get(struct a4l_subdevice *subd, 
			unsigned long count);

int a4l_buf_commit_get(struct a4l_subdevice *subd, 
		       unsigned long count);

int a4l_buf_get(struct a4l_subdevice *subd,
		void *bufdata, unsigned long count);

int a4l_buf_evt(struct a4l_subdevice *subd, unsigned long evts);

unsigned long a4l_buf_count(struct a4l_subdevice *subd);

/* --- Current Command management function --- */

a4l_cmd_t *a4l_get_cmd(struct a4l_subdevice *subd);

/* --- Munge related function --- */

int a4l_get_chan(struct a4l_subdevice *subd);

/* --- IOCTL / FOPS functions --- */

int a4l_ioctl_mmap(a4l_cxt_t * cxt, void *arg);
int a4l_ioctl_bufcfg(a4l_cxt_t * cxt, void *arg);
int a4l_ioctl_bufinfo(a4l_cxt_t * cxt, void *arg);
int a4l_ioctl_poll(a4l_cxt_t * cxt, void *arg);
ssize_t a4l_read(a4l_cxt_t * cxt, void *bufdata, size_t nbytes);
ssize_t a4l_write(a4l_cxt_t * cxt, 
		  const void *bufdata, size_t nbytes);
int a4l_select(a4l_cxt_t *cxt, 
	       rtdm_selector_t *selector,
	       enum rtdm_selecttype type, unsigned fd_index);

#endif /* __KERNEL__ */

/* MMAP ioctl argument structure */
struct a4l_mmap_arg {
	unsigned int idx_subd;
	unsigned long size;
	void *ptr;
};
typedef struct a4l_mmap_arg a4l_mmap_t;

/* Constants related with buffer size
   (might be used with BUFCFG ioctl) */
#define A4L_BUF_MAXSIZE 0x1000000
#define A4L_BUF_DEFSIZE 0x10000

/* BUFCFG ioctl argument structure */
struct a4l_buffer_config {
	unsigned int idx_subd;
	unsigned long buf_size;
};
typedef struct a4l_buffer_config a4l_bufcfg_t;

/* BUFINFO ioctl argument structure */
struct a4l_buffer_info {
	unsigned int idx_subd;
	unsigned long buf_size;
	unsigned long rw_count;
};
typedef struct a4l_buffer_info a4l_bufinfo_t;

/* POLL ioctl argument structure */
struct a4l_poll {
	unsigned int idx_subd;
	unsigned long arg;
};
typedef struct a4l_poll a4l_poll_t;

#endif /* !DOXYGEN_CPP */

#endif /* __ANALOGY_BUFFER_H__ */

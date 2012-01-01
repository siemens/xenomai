/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _XENO_POSIX_MQUEUE_H
#define _XENO_POSIX_MQUEUE_H

#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/signal.h>
#include <linux/fcntl.h>

#else /* !__KERNEL__ */

#include <xeno_config.h>
#include <fcntl.h>

#endif /* !__KERNEL__ */

#if defined(__KERNEL__) || !defined(HAVE_MQUEUE_H)

#ifndef MQ_PRIO_MAX
#define MQ_PRIO_MAX 32768
#endif

#ifndef __KERNEL__
typedef unsigned long mqd_t;
#endif /* !__KERNEL__ */

struct mq_attr {
    long    mq_flags;
    long    mq_maxmsg;
    long    mq_msgsize;
    long    mq_curmsgs;
};

#else /* !(__KERNEL__ || !HAVE_MQUEUE_H) */

#include_next <mqueue.h>
#include <cobalt/wrappers.h>

#ifdef __cplusplus
extern "C" {
#endif

COBALT_DECL(int, open(const char *path, int oflag, ...));

COBALT_DECL(mqd_t, mq_open(const char *name,
			   int oflags,
			   ...));

COBALT_DECL(int, mq_close(mqd_t qd));

COBALT_DECL(int, mq_unlink(const char *name));

COBALT_DECL(int, mq_getattr(mqd_t qd,
			    struct mq_attr *attr));

COBALT_DECL(int, mq_setattr(mqd_t qd,
			    const struct mq_attr *__restrict__ attr,
			    struct mq_attr *__restrict__ oattr));

COBALT_DECL(int, mq_send(mqd_t qd,
			 const char *buffer,
			 size_t len,
			 unsigned prio));

COBALT_DECL(int, mq_timedsend(mqd_t q,
			      const char * buffer,
			      size_t len,
			      unsigned prio,
			      const struct timespec *timeout));

COBALT_DECL(ssize_t, mq_receive(mqd_t q,
				char *buffer,
				size_t len,
				unsigned *prio));

COBALT_DECL(ssize_t, mq_timedreceive(mqd_t q,
				     char *__restrict__ buffer,
				     size_t len,
				     unsigned *__restrict__ prio,
				     const struct timespec *__restrict__ timeout));

COBALT_DECL(int, mq_notify(mqd_t mqdes,
			   const struct sigevent *notification));

#ifdef __cplusplus
}
#endif

#endif /* !(__KERNEL__ || !HAVE_MQUEUE_H) */

#endif /* _XENO_POSIX_MQUEUE_H */

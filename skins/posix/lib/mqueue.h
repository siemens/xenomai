/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _XENO_POSIX_MQUEUE_H
#define _XENO_POSIX_MQUEUE_H

#include <xeno_config.h>
#include <fcntl.h>              /* For mq_open flags. */

#ifdef HAVE_MQUEUE_H

#include_next <mqueue.h>

#ifdef __cplusplus
extern "C" {
#endif

mqd_t __real_mq_open(const char *name,
		     int oflags,
		     ...);

int __real_mq_close(mqd_t qd);

int __real_mq_unlink(const char *name);

int __real_mq_getattr(mqd_t qd,
		      struct mq_attr *attr);

int __real_mq_setattr(mqd_t qd,
		      const struct mq_attr *__restrict__ attr,
		      struct mq_attr *__restrict__ oattr);

int __real_mq_send(mqd_t qd,
		   const char *buffer,
		   size_t len,
		   unsigned prio);

int __real_mq_timedsend(mqd_t q,
			const char * buffer,
			size_t len,
			unsigned prio,
			const struct timespec *timeout);

ssize_t __real_mq_receive(mqd_t q,
			  char *buffer,
			  size_t len,
			  unsigned *prio);

ssize_t __real_mq_timedreceive(mqd_t q,
			       char *__restrict__ buffer,
			       size_t len,
			       unsigned *__restrict__ prio,
			       const struct timespec *__restrict__ timeout);
#ifdef __cplusplus
}
#endif

#else /* !HAVE_MQUEUE_H */

typedef unsigned long mqd_t;

struct mq_attr {
    long    mq_flags;
    long    mq_maxmsg;
    long    mq_msgsize;
    long    mq_curmsgs;
};

#ifdef __cplusplus
extern "C" {
#endif

mqd_t mq_open(const char *name,
	      int oflags,
	      ...);

int mq_close(mqd_t qd);

int mq_unlink(const char *name);

int mq_getattr(mqd_t qd,
	       struct mq_attr *attr);

int mq_setattr(mqd_t qd,
	       const struct mq_attr *__restrict__ attr,
	       struct mq_attr *__restrict__ oattr);

int mq_send(mqd_t qd,
	    const char *buffer,
	    size_t len,
	    unsigned prio);

int mq_timedsend(mqd_t q,
		 const char * buffer,
		 size_t len,
		 unsigned prio,
		 const struct timespec *timeout);

ssize_t mq_receive(mqd_t q,
		   char *buffer,
		   size_t len,
		   unsigned *prio);

ssize_t mq_timedreceive(mqd_t q,
			char *__restrict__ buffer,
			size_t len,
			unsigned *__restrict__ prio,
			const struct timespec *__restrict__ timeout);
#ifdef __cplusplus
}
#endif

#endif /* HAVE_MQUEUE_H */

#endif /* _XENO_POSIX_MQUEUE_H */

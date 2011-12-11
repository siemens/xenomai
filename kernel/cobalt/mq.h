#ifndef POSIX_MQ_H
#define POSIX_MQ_H

#include <nucleus/queue.h>
#include "registry.h"     /* For associative lists. */

struct cobalt_mq;
typedef struct cobalt_mq cobalt_mq_t;

int cobalt_mq_select_bind(mqd_t fd, struct xnselector *selector,
			  unsigned type, unsigned index);

int cobalt_mq_open(const char __user *u_name, int oflags,
		   mode_t mode, struct mq_attr __user *u_attr, mqd_t uqd);

int cobalt_mq_close(mqd_t uqd);

int cobalt_mq_unlink(const char __user *u_name);

int cobalt_mq_getattr(mqd_t uqd, struct mq_attr __user *u_attr);

int cobalt_mq_setattr(mqd_t uqd, const struct mq_attr __user *u_attr,
		      struct mq_attr __user *u_oattr);

int cobalt_mq_send(mqd_t uqd,
		   const void __user *u_buf, size_t len, unsigned int prio);

int cobalt_mq_timedsend(mqd_t uqd, const void __user *u_buf, size_t len,
			unsigned int prio, const struct timespec __user *u_ts);

int cobalt_mq_receive(mqd_t uqd, void __user *u_buf,
		      ssize_t __user *u_len, unsigned int __user *u_prio);

int cobalt_mq_timedreceive(mqd_t uqd, void __user *u_buf,
			   ssize_t __user *u_len,
			   unsigned int __user *u_prio,
			   const struct timespec __user *u_ts);

#ifndef __XENO_SIM__
void cobalt_mq_uqds_cleanup(cobalt_queues_t *q);
#endif

int cobalt_mq_pkg_init(void);

void cobalt_mq_pkg_cleanup(void);

#endif /* POSIX_MQ_H */

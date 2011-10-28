#ifndef POSIX_MQ_H
#define POSIX_MQ_H

#include <nucleus/queue.h>
#include "registry.h"     /* For associative lists. */

struct cobalt_mq;
typedef struct cobalt_mq cobalt_mq_t;

typedef struct cobalt_msg {
	xnpholder_t link;
	size_t len;
	char data[0];
} cobalt_msg_t;

#define cobalt_msg_get_prio(msg) (msg)->link.prio
#define cobalt_msg_set_prio(msg, prio) (msg)->link.prio = (prio)

cobalt_msg_t *cobalt_mq_timedsend_inner(cobalt_mq_t **mqp, mqd_t fd, size_t len,
				      const struct timespec *abs_timeoutp);

int cobalt_mq_finish_send(mqd_t fd, cobalt_mq_t *mq, cobalt_msg_t *msg);

cobalt_msg_t *cobalt_mq_timedrcv_inner(cobalt_mq_t **mqp, mqd_t fd, size_t len,
				     const struct timespec *abs_timeoutp);

int cobalt_mq_finish_rcv(mqd_t fd, cobalt_mq_t *mq, cobalt_msg_t *msg);

#ifndef __XENO_SIM__
void cobalt_mq_uqds_cleanup(cobalt_queues_t *q);
#endif

int cobalt_mq_pkg_init(void);

void cobalt_mq_pkg_cleanup(void);

#endif /* POSIX_MQ_H */

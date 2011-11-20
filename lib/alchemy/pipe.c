/*
 * Copyright (C) 2011 Philippe Gerum <rpm@xenomai.org>.
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

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include "rtdm/rtipc.h"
#include "copperplate/threadobj.h"
#include "copperplate/heapobj.h"
#include "copperplate/cluster.h"
#include "reference.h"
#include "internal.h"
#include "pipe.h"
#include "timer.h"

struct syncluster alchemy_pipe_table;

static struct alchemy_namegen pipe_namegen = {
	.prefix = "pipe",
	.length = sizeof ((struct alchemy_pipe *)0)->name,
};

static struct alchemy_pipe *find_alchemy_pipe(RT_PIPE *pipe, int *err_r)
{
	struct alchemy_pipe *pcb;

	if (pipe == NULL || ((intptr_t)pipe & (sizeof(intptr_t)-1)) != 0)
		goto bad_handle;

	pcb = mainheap_deref(pipe->handle, struct alchemy_pipe);
	if (pcb == NULL || ((intptr_t)pcb & (sizeof(intptr_t)-1)) != 0)
		goto bad_handle;

	if (pcb->magic == pipe_magic)
		return pcb;
bad_handle:
	*err_r = -EINVAL;

	return NULL;
}

int rt_pipe_create(RT_PIPE *pipe,
		   const char *name, int minor, size_t poolsize)
{
	struct rtipc_port_label plabel;
	struct sockaddr_ipc saddr;
	struct alchemy_pipe *pcb;
	struct service svc;
	size_t streambufsz;
	int ret, sock;

	if (threadobj_irq_p())
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	pcb = xnmalloc(sizeof(*pcb));
	if (pcb == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	sock = __RT(socket(AF_RTIPC, SOCK_DGRAM, IPCPROTO_XDDP));
	if (sock < 0) {
		warning("RTIPC/XDDP protocol not supported by kernel");
		ret = -errno;
		xnfree(pcb);
		goto out;
	}

	if (name && *name) {
		strncpy(plabel.label, name, sizeof(plabel.label)-1);
		plabel.label[sizeof(plabel.label)-1] = '\0';
		ret = __RT(setsockopt(sock, SOL_XDDP, XDDP_LABEL,
				      &plabel, sizeof(plabel)));
		if (ret)
			goto fail_sockopt;
	}

	if (poolsize > 0) {
		ret = setsockopt(pcb->sock, SOL_XDDP, XDDP_POOLSZ,
				 &poolsize, sizeof(poolsize));
		if (ret)
			goto fail_sockopt;
	}

	streambufsz = ALCHEMY_PIPE_STREAMSZ;
	ret = __RT(setsockopt(pcb->sock, SOL_XDDP, XDDP_BUFSZ,
			      &streambufsz, streambufsz));
	if (ret)
		goto fail_sockopt;

	memset(&saddr, 0, sizeof(saddr));
	saddr.sipc_family = AF_RTIPC;
	saddr.sipc_port = minor;
	ret = bind(sock, (struct sockaddr *)&saddr, sizeof(saddr));
	if (ret)
		goto fail_sockopt;

	alchemy_build_name(pcb->name, name, &pipe_namegen);
	pcb->sock = sock;
	pcb->magic = pipe_magic;

	if (syncluster_addobj(&alchemy_pipe_table, pcb->name, &pcb->cobj)) {
		ret = -EEXIST;
		goto fail_register;
	}

	pipe->handle = mainheap_ref(pcb, uintptr_t);

	COPPERPLATE_UNPROTECT(svc);

	return 0;
fail_sockopt:
	ret = -errno;
fail_register:
	__RT(close(sock));
	xnfree(pcb);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;	
}

int rt_pipe_delete(RT_PIPE *pipe)
{
	struct alchemy_pipe *pcb;
	struct service svc;
	int ret = 0;

	if (threadobj_irq_p())
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	pcb = find_alchemy_pipe(pipe, &ret);
	if (pcb == NULL)
		goto out;

	ret = __RT(close(pcb->sock));
	if (ret) {
		ret = -errno;
		if (ret == -EBADF)
			ret = -EIDRM;
		goto out;
	}

	syncluster_delobj(&alchemy_pipe_table, &pcb->cobj);
	pcb->magic = ~pipe_magic;
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

ssize_t rt_pipe_read_until(RT_PIPE *pipe,
			   void *buf, size_t size, RTIME timeout)
{
	struct timeval tv = { .tv_sec = 0, .tv_usec = 0 };
	struct timespec ts, *timespec;
	struct alchemy_pipe *pcb;
	struct service svc;
	int err, flags;
	ssize_t ret;

	if (timeout != TM_NONBLOCK && !threadobj_current_p())
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	pcb = find_alchemy_pipe(pipe, &err);
	if (pcb == NULL) {
		ret = err;
		goto out;
	}

	switch (timeout) {
	default:
		timespec = alchemy_get_timespec(timeout, &ts);
		tv.tv_sec = ts.tv_sec;
		tv.tv_usec = ts.tv_nsec / 1000;
		/* Falldown wanted. */
	case TM_INFINITE:
		__RT(setsockopt(pcb->sock, SOL_SOCKET,
				SO_RCVTIMEO, &tv, sizeof(tv)));
		flags = 0;
		break;
	case TM_NONBLOCK:
		flags = MSG_DONTWAIT;
		break;
	}

	ret = __RT(recvfrom(pcb->sock, buf, size, flags, NULL, 0));
	if (ret < 0)
		ret = -errno;
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

ssize_t rt_pipe_read(RT_PIPE *pipe,
		     void *buf, size_t size, RTIME timeout)
{
	timeout = alchemy_rel2abs_timeout(timeout);
	return rt_pipe_read_until(pipe, buf, size, timeout);
}

static ssize_t do_write_pipe(RT_PIPE *pipe,
			     const void *buf, size_t size, int flags)
{
	struct alchemy_pipe *pcb;
	struct service svc;
	ssize_t ret;
	int err;

	COPPERPLATE_PROTECT(svc);

	pcb = find_alchemy_pipe(pipe, &err);
	if (pcb == NULL) {
		ret = err;
		goto out;
	}

	ret = __RT(sendto(pcb->sock, buf, size, flags, NULL, 0));
	if (ret < 0)
		ret = -errno;
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

ssize_t rt_pipe_write(RT_PIPE *pipe,
		      const void *buf, size_t size, int mode)
{
	int flags = 0;

	if (mode & P_URGENT)
		flags |= MSG_OOB;

	return do_write_pipe(pipe, buf, size, flags);
}

ssize_t rt_pipe_stream(RT_PIPE *pipe,
		       const void *buf, size_t size)
{
	return do_write_pipe(pipe, buf, size, MSG_MORE);
}

int rt_pipe_bind(RT_PIPE *pipe,
		 const char *name, RTIME timeout)
{
	return alchemy_bind_object(name,
				   &alchemy_pipe_table,
				   timeout,
				   offsetof(struct alchemy_pipe, cobj),
				   &pipe->handle);
}

int rt_pipe_unbind(RT_PIPE *pipe)
{
	pipe->handle = 0;
	return 0;
}

/*
 * This file is part of the Xenomai project.
 *
 * Copyright (C) 2008 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "thread.h"
#include "apc.h"

#define COBALT_LO_MAX_REQUESTS 64	/* Must be a ^2 */

static int cobalt_lostage_apc;

static struct cobalt_lostageq_t {
	int in, out;
	struct cobalt_lostage_req_t {
		int type;
		void *arg;
		size_t size;
	} req[COBALT_LO_MAX_REQUESTS];
} cobalt_lostageq[XNARCH_NR_CPUS];

void cobalt_schedule_lostage(int request, void *arg, size_t size)
{
	int cpuid = ipipe_processor_id(), reqnum;
	struct cobalt_lostageq_t *rq = &cobalt_lostageq[cpuid];
	spl_t s;

	/* Signal the APC, to have it delegate signals to Linux. */
	splhigh(s);
	reqnum = rq->in;
	rq->req[reqnum].type = request;
	rq->req[reqnum].arg = arg;
	rq->req[reqnum].size = size;
	rq->in = (reqnum + 1) & (COBALT_LO_MAX_REQUESTS - 1);
	__rthal_apc_schedule(cobalt_lostage_apc);
	splexit(s);
}

static void cobalt_lostage_handle_request(void *cookie)
{
	int cpuid = smp_processor_id(), reqnum;
	struct cobalt_lostageq_t *rq = &cobalt_lostageq[cpuid];

	while ((reqnum = rq->out) != rq->in) {
		struct cobalt_lostage_req_t *req = &rq->req[reqnum];

		rq->out = (reqnum + 1) & (COBALT_LO_MAX_REQUESTS - 1);

		if (req->type == COBALT_LO_FREE_REQ)
			xnarch_free_host_mem(req->arg, req->size);
	}
}

int cobalt_apc_pkg_init(void)
{
	cobalt_lostage_apc = rthal_apc_alloc("cobalt_lostage_handler",
					    &cobalt_lostage_handle_request, NULL);

	if (cobalt_lostage_apc < 0)
		printk("Unable to allocate APC: %d !\n", cobalt_lostage_apc);

	return cobalt_lostage_apc < 0;
}

void cobalt_apc_pkg_cleanup(void)
{
	rthal_apc_free(cobalt_lostage_apc);
}

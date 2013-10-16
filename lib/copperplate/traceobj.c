/*
 * Copyright (C) 2013 Philippe Gerum <rpm@xenomai.org>.
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
#include <stdio.h>
#include <stdlib.h>
#include "boilerplate/lock.h"
#include "copperplate/traceobj.h"
#include "copperplate/threadobj.h"
#include "copperplate/heapobj.h"
#include "copperplate/init.h"
#include "internal.h"

struct tracemark {
	const char *file;
	int line;
	int mark;
};

void traceobj_init(struct traceobj *trobj, const char *label, int nr_marks)
{
	pthread_mutexattr_t mattr;
	pthread_condattr_t cattr;

	__RT(pthread_mutexattr_init(&mattr));
	__RT(pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT));
	__RT(pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_PRIVATE));
	__RT(pthread_mutex_init(&trobj->lock, &mattr));
	__RT(pthread_mutexattr_destroy(&mattr));
	__RT(pthread_condattr_init(&cattr));
	__RT(pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_PRIVATE));
	__RT(pthread_condattr_setclock(&cattr, CLOCK_COPPERPLATE));
	__RT(pthread_cond_init(&trobj->join, &cattr));
	__RT(pthread_condattr_destroy(&cattr));
	/*
	 * We make sure not to unblock from threadobj_join() until at
	 * least one thread has called trace_enter() for this trace
	 * object.
	 */
	trobj->nr_threads = -1;

	trobj->label = label;
	trobj->nr_marks = nr_marks;
	trobj->cur_mark = 0;

	if (nr_marks > 0) {
		trobj->marks = pvmalloc(sizeof(struct tracemark) * nr_marks);
		if (trobj->marks == NULL)
			panic("cannot allocate mark table for tracing");
	}
}

static void compare_marks(struct traceobj *trobj, int tseq[], int nr_seq) /* lock held */
{
	int mark;

	for (mark = 0; mark < trobj->cur_mark || mark < nr_seq; mark++) {
		if (mark >= trobj->cur_mark) {
			fprintf(stderr, " <missing mark> |  [%d] expected\n",
				tseq[mark]);
		} else if (mark < nr_seq)
			__RT(fprintf(stderr, "at %s:%d  |  [%d] should be [%d]\n",
				     trobj->marks[mark].file,
				     trobj->marks[mark].line,
				     trobj->marks[mark].mark,
				     tseq[mark]));
		else
			__RT(fprintf(stderr, "at %s:%d  |  unexpected [%d]\n",
				     trobj->marks[mark].file,
				     trobj->marks[mark].line,
				     trobj->marks[mark].mark));
	}

	fflush(stderr);
}

void traceobj_verify(struct traceobj *trobj, int tseq[], int nr_seq)
{
	int end_mark, mark, state;

	read_lock_safe(&trobj->lock, state);

	if (nr_seq > trobj->nr_marks)
		goto fail;

	end_mark = trobj->cur_mark;
	if (end_mark == 0) {
		read_unlock_safe(&trobj->lock, state);
		panic("no mark defined");
	}

	if (end_mark != nr_seq)
		goto fail;

	for (mark = 0; mark < end_mark; mark++) {
		if (trobj->marks[mark].mark != tseq[mark])
			goto fail;
	}

	read_unlock_safe(&trobj->lock, state);
	return;

fail:
	warning("mismatching execution sequence detected");
	compare_marks(trobj, tseq, nr_seq);
	read_unlock_safe(&trobj->lock, state);
#ifdef CONFIG_XENO_MERCURY
	/*
	 * The Mercury core does not force any affinity, which may
	 * lead to wrong results with some unit tests checking strict
	 * ordering of operations. Tell the user about this. Normally,
	 * such unit tests on Mercury should be pinned on a single CPU
	 * using --cpu-affinity.
	 */
	if (CPU_COUNT(&__node_info.cpu_affinity) == 0)
		warning("NOTE: --cpu-affinity option was not given - this might explain?");
#endif
#ifndef CONFIG_XENO_ASYNC_CANCEL
	/*
	 * Lack of async cancellation support might also explain why
	 * some tests have failed.
	 */
	warning("NOTE: --disable-async-cancel option was given - this might explain?");
#endif
	exit(5);
}

void traceobj_destroy(struct traceobj *trobj)
{
	pvfree(trobj->marks);
	__RT(pthread_mutex_destroy(&trobj->lock));
}

static void dump_marks(struct traceobj *trobj) /* lock held */
{
	int mark;

	for (mark = 0; mark < trobj->cur_mark; mark++)
		fprintf(stderr, "[%d] at %s:%d\n",
			trobj->marks[mark].mark,
			trobj->marks[mark].file,
			trobj->marks[mark].line);

	fflush(stderr);
}

void __traceobj_assert_failed(struct traceobj *trobj,
			      const char *file, int line, const char *cond)
{
	push_cleanup_lock(&trobj->lock);
	read_lock(&trobj->lock);
	dump_marks(trobj);
	read_unlock(&trobj->lock);
	pop_cleanup_lock(&trobj->lock);

	panic("trace assertion failed:\n%s:%d => \"%s\"", file, line, cond);
}

void __traceobj_mark(struct traceobj *trobj,
		     const char *file, int line, int mark)
{
	struct tracemark *tmk;
	int cur_mark;

	pthread_testcancel();
	push_cleanup_lock(&trobj->lock);
	write_lock(&trobj->lock);

	cur_mark = trobj->cur_mark;
	if (cur_mark >= trobj->nr_marks) {
		dump_marks(trobj);
		panic("too many marks: [%d] at %s:%d", mark, file, line);
	}

	tmk = trobj->marks + cur_mark;
	tmk->file = file;
	tmk->line = line;
	tmk->mark = mark;
	trobj->cur_mark++;

	write_unlock(&trobj->lock);
	pop_cleanup_lock(&trobj->lock);
}

void traceobj_enter(struct traceobj *trobj)
{
	struct threadobj *current = threadobj_current();

	if (current) {
		threadobj_lock(current);
		current->tracer = trobj;
		threadobj_unlock(current);
	}

	/*
	 * Our caller is usually out of any protected section, so push
	 * a cleanup routine.
	 */
	push_cleanup_lock(&trobj->lock);
	write_lock(&trobj->lock);
	if (++trobj->nr_threads == 0)
		trobj->nr_threads = 1;
	write_unlock(&trobj->lock);
	pop_cleanup_lock(&trobj->lock);
}

/* May be directly called from finalizer. */
void traceobj_unwind(struct traceobj *trobj)
{
	int state;

	write_lock_safe(&trobj->lock, state);

	if (--trobj->nr_threads <= 0)
		__RT(pthread_cond_signal(&trobj->join));

	write_unlock_safe(&trobj->lock, state);
}

void traceobj_exit(struct traceobj *trobj)
{
	struct threadobj *current = threadobj_current();

	if (current)
		current->tracer = NULL;

	traceobj_unwind(trobj);
}

void traceobj_join(struct traceobj *trobj)
{
	push_cleanup_lock(&trobj->lock);
	read_lock(&trobj->lock);

	while (trobj->nr_threads < 0 || trobj->nr_threads > 0)
		__RT(pthread_cond_wait(&trobj->join, &trobj->lock));

	read_unlock(&trobj->lock);
	pop_cleanup_lock(&trobj->lock);
}

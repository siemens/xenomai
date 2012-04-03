#include <stdlib.h>		/* exit */

#include <unistd.h>		/* sleep */
#include <sys/mman.h>		/* mlockall */

#include <native/alarm.h>
#include <native/buffer.h>
#include <native/cond.h>
#include <native/event.h>
#include <native/heap.h>
#include <native/mutex.h>
#include <native/pipe.h>
#include <native/queue.h>
#include <native/sem.h>
#include <native/task.h>

#include <nucleus/heap.h>
#include <rtdk.h>

#include "check.h"

#define check_used(object, before, failed)				\
	({								\
		unsigned long long after = get_used();			\
		if (before != after) {					\
			rt_fprintf(stderr, object		\
				" leaked %Lu bytes\n", after-before);	\
			failed = 1;					\
		} else							\
			rt_fprintf(stderr, object ": OK\n");		\
	})

unsigned long long get_used(void)
{
	unsigned long long used = 0;
	struct xnheap_desc hd;
	int i;

	for (i = 0; XENOMAI_SYSCALL2(__xn_sys_heap_info, &hd, i) == 0; i++)
		used += hd.used;

	if (used == 0) {
		rt_fprintf(stderr, "Error: could not get size of used memory\n");
		exit(EXIT_FAILURE);
	}

	return used;
}

void empty(void *cookie)
{
}

int main(void)
{
	unsigned long long before;
	RT_ALARM nalrm;
	RT_BUFFER nbuf;
	RT_COND ncond;
	RT_EVENT nevt;
	RT_HEAP nheap;
	RT_MUTEX nmtx;
	RT_PIPE npipe;
	RT_QUEUE nq;
	RT_SEM nsem;
	RT_TASK ntsk;
	int failed = 0;

	mlockall(MCL_CURRENT|MCL_FUTURE);

	rt_print_auto_init(1);

	rt_fprintf(stderr, "Checking for leaks in native skin services\n");
	before = get_used();
	check_native(rt_alarm_create(&nalrm, NULL));
	check_native(rt_alarm_delete(&nalrm));
	check_used("alarm", before, failed);

	before = get_used();
	check_native(rt_buffer_create(&nbuf, NULL, 16384, B_PRIO));
	check_native(rt_buffer_delete(&nbuf));
	check_used("buffer", before, failed);

	before = get_used();
	check_native(rt_cond_create(&ncond, NULL));
	check_native(rt_cond_delete(&ncond));
	check_used("cond", before, failed);

	before = get_used();
	check_native(rt_event_create(&nevt, NULL, 0, EV_PRIO));
	check_native(rt_event_delete(&nevt));
	check_used("event", before, failed);

	before = get_used();
	check_native(rt_heap_create(&nheap, "heap", 16384, H_PRIO | H_SHARED));
	check_native(rt_heap_delete(&nheap));
	check_used("heap", before, failed);

	before = get_used();
	check_native(rt_mutex_create(&nmtx, NULL));
	check_native(rt_mutex_delete(&nmtx));
	check_used("mutex", before, failed);

	before = get_used();
	check_native(rt_pipe_create(&npipe, NULL, P_MINOR_AUTO, 0));
	check_native(rt_pipe_delete(&npipe));
	check_used("pipe", before, failed);

	before = get_used();
	check_native(rt_queue_create(&nq, "queue", 16384, Q_UNLIMITED, Q_PRIO));
	check_native(rt_queue_delete(&nq));
	check_used("queue", before, failed);

	before = get_used();
	check_native(rt_sem_create(&nsem, NULL, 0, S_PRIO));
	check_native(rt_sem_delete(&nsem));
	check_used("sem", before, failed);

	before = get_used();
	check_native(rt_task_spawn(&ntsk, NULL, 0, 1, T_JOINABLE, empty, NULL));
	check_native(rt_task_join(&ntsk));
	sleep(1);		/* Leave some time for xnheap
				 * deferred free */
	check_used("task", before, failed);

	return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

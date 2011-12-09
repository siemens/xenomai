#include <stdio.h>
#include <stdlib.h>
#include <copperplate/init.h>
#include <copperplate/traceobj.h>
#include <alchemy/task.h>
#include <alchemy/heap.h>

static struct traceobj trobj;

static RT_TASK t_bgnd, t_fgnd;

static void background_task(void *arg)
{
	void *p1, *p2;
	RT_HEAP heap;
	int ret;

	ret = rt_heap_bind(&heap, "HEAP", TM_INFINITE);
	traceobj_assert(&trobj, ret == 0);

	traceobj_enter(&trobj);

	ret = rt_heap_alloc(&heap, 8192, TM_NONBLOCK, &p1);
	traceobj_assert(&trobj, ret == -EWOULDBLOCK);
	ret = rt_heap_alloc(&heap, 8192, TM_INFINITE, &p1);
	traceobj_assert(&trobj, ret == 0);
	ret = rt_heap_alloc(&heap, 8192, TM_NONBLOCK, &p2);
	traceobj_assert(&trobj, ret == 0);
	ret = rt_heap_alloc(&heap, 8192, TM_INFINITE, &p1);
	traceobj_assert(&trobj, ret == -EIDRM);

	traceobj_exit(&trobj);
}

static void foreground_task(void *arg)
{
	void *p1, *p2;
	RT_HEAP heap;
	int ret;

	ret = rt_heap_bind(&heap, "HEAP", TM_INFINITE);
	traceobj_assert(&trobj, ret == 0);

	traceobj_enter(&trobj);

	ret = rt_heap_alloc(&heap, 8192, TM_NONBLOCK, &p1);
	traceobj_assert(&trobj, ret == 0);
	ret = rt_heap_alloc(&heap, 8192, TM_NONBLOCK, &p2);
	traceobj_assert(&trobj, ret == 0);

	ret = rt_task_set_priority(NULL, 19);
	traceobj_assert(&trobj, ret == 0);
	ret = rt_task_set_priority(NULL, 21);
	traceobj_assert(&trobj, ret == 0);

	ret = rt_heap_free(&heap, p1);
	traceobj_assert(&trobj, ret == 0);
	ret = rt_heap_free(&heap, p2);
	traceobj_assert(&trobj, ret == 0);

	rt_task_sleep(1000000);

	ret = rt_heap_delete(&heap);
	traceobj_assert(&trobj, ret == 0);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	RT_HEAP heap;
	int ret;

	copperplate_init(&argc, &argv);

	traceobj_init(&trobj, argv[0], 0);

	ret = rt_task_create(&t_bgnd, "BGND", 0,  20, 0);
	traceobj_assert(&trobj, ret == 0);

	ret = rt_task_start(&t_bgnd, background_task, NULL);
	traceobj_assert(&trobj, ret == 0);

	ret = rt_task_create(&t_fgnd, "FGND", 0,  21, 0);
	traceobj_assert(&trobj, ret == 0);

	ret = rt_task_start(&t_fgnd, foreground_task, NULL);
	traceobj_assert(&trobj, ret == 0);

	ret = rt_heap_create(&heap, "HEAP", 16384, H_PRIO);
	traceobj_assert(&trobj, ret == 0);

	traceobj_join(&trobj);

	exit(0);
}

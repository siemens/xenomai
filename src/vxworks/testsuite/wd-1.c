#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <vxworks/errnoLib.h>
#include <vxworks/taskLib.h>
#include <vxworks/wdLib.h>
#include <vxworks/intLib.h>
#include <vxworks/kernelLib.h>

static struct traceobj trobj;

static int tseq[] = {
	5, 6, 8,
	1, 4, 1, 4, 1,
	2, 3, 7
};

TASK_ID tid;

WDOG_ID wdog_id;

void watchdogHandler(long arg)
{
	static int hits;
	int ret;

	traceobj_assert(&trobj, arg == 0xfefbfcfd);

	ret = intContext();
	traceobj_assert(&trobj, ret);

	traceobj_mark(&trobj, 1);

	if (++hits >= 3) {
		ret = wdCancel(wdog_id);
		traceobj_assert(&trobj, ret == OK);
		traceobj_mark(&trobj, 2);
		ret = taskResume(tid);
		traceobj_assert(&trobj, ret == OK);
		traceobj_mark(&trobj, 3);
		return;
	}

	traceobj_mark(&trobj, 4);
	ret = wdStart(wdog_id, 200, watchdogHandler, arg);
	traceobj_assert(&trobj, ret == OK);
}

void rootTask(long a0, long a1, long a2, long a3, long a4,
	      long a5, long a6, long a7, long a8, long a9)
{
	int ret;

	traceobj_enter(&trobj);

	tid = taskIdSelf();

	traceobj_mark(&trobj, 5);

	wdog_id = wdCreate();
	traceobj_assert(&trobj, wdog_id != 0);

	ret = wdStart(wdog_id, 200, watchdogHandler, 0xfefbfcfd);
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 6);

	ret = taskSuspend(tid);
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 7);

	ret = wdDelete(wdog_id);
	traceobj_assert(&trobj, ret == OK);

	traceobj_exit(&trobj);
}

int main(int argc, char *argv[])
{
	int ret;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	ret = kernelInit(rootTask, argc, argv);

	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 8);

	traceobj_join(&trobj);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	exit(0);
}

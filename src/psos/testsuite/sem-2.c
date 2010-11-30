#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <psos/psos.h>

static struct traceobj trobj;

static int tseq[] = {
	1, 2, 3, 5, 4, 6
};

u_long tidA, sem_id;

void task_A(u_long a0, u_long a1, u_long a2, u_long a3)
{
	int ret;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 1);

	ret = sm_create("SEM", 1, SM_FIFO, &sem_id);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 2);

	ret = sm_p(sem_id, SM_NOWAIT, 0);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 3);

	ret = sm_p(sem_id, SM_WAIT, 0);
	traceobj_assert(&trobj, ret == ERR_SKILLD);

	traceobj_mark(&trobj, 4);

	traceobj_exit(&trobj);
}

int main(int argc, char *argv[])
{
	u_long args[] = { 1, 2, 3, 4 };
	int ret;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	ret = PSOS_INIT(argc, argv);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_create("TSKA", 20, 0, 0, 0, &tidA);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_start(tidA, 0, task_A, args);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 5);

	ret = sm_delete(sem_id);
	traceobj_assert(&trobj, ret == ERR_TATSDEL);

	traceobj_mark(&trobj, 6);

	traceobj_join(&trobj);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	exit(0);
}

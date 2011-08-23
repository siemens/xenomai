#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <psos/psos.h>

static struct traceobj trobj;

u_long tidA, tidB;

void task_A(u_long a1, u_long a2, u_long a3, u_long a4)
{
	/* NOT STARTED */
}

void task_B(u_long a1, u_long a2, u_long a3, u_long a4)
{
	/* NOT STARTED */
}

int main(int argc, char *argv[])
{
	u_long tid;
	int ret;

	traceobj_init(&trobj, argv[0], 0);

	ret = PSOS_INIT(argc, argv);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_create("TSKA", 20, 0, 0, 0, &tidA);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_create("TSKB", 21, 0, 0, 0, &tidB);
	traceobj_assert(&trobj, ret == SUCCESS);

	tid = ~tidA;
	ret = t_ident("TSKA", 0, &tid);
	traceobj_assert(&trobj, ret == SUCCESS);
	traceobj_assert(&trobj, tid == tidA);

	tid = ~tidB;
	ret = t_ident("TSKB", 0, &tid);
	traceobj_assert(&trobj, ret == SUCCESS);
	traceobj_assert(&trobj, tid == tidB);

	ret = t_delete(tidA);
	traceobj_assert(&trobj, ret == SUCCESS);
	ret = t_ident("TSKA", 0, &tid);
	traceobj_assert(&trobj, ret == ERR_OBJNF);

	ret = t_ident("TSKB", 1, &tid);
	traceobj_assert(&trobj, ret == ERR_NODENO);

	exit(0);
}

/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Adapted from CarbonKernel
 * Copyright (C) 2001  Philippe Gerum.<rpm@xenomai.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Description: Testing VxWorks services:
 * - taskActivate
 * - taskDelete
 * - taskDeleteForce
 * - taskIdVerify
 * - taskInit
 * - taskPriorityGet
 * - taskPrioritySet
 * - taskTcb
 * - taskSafe
 * - taskUnsafe
 *
 */

#include <vxworks_test.h>

static WIND_TCB peerTcb;

void peerTask  (long a0, long a1, long a2, long a3, long a4,
		long a5, long a6, long a7, long a8, long a9)
{
    WIND_TCB *pTcb = taskTcb(taskIdSelf());
    TEST_ASSERT(pTcb == &peerTcb);

    TEST_MARK();

    TEST_ASSERT_OK(taskSafe());

    TEST_MARK();

    TEST_ASSERT_OK(taskPrioritySet(taskIdSelf(),21));

    TEST_MARK();

    TEST_ASSERT_OK(taskUnsafe());

    TEST_MARK();
}

void rootTask (long a0, long a1, long a2, long a3, long a4,
	       long a5, long a6, long a7, long a8, long a9)
{
    const size_t stackSize = 32768;
    char *pstackBase = NULL;
    WIND_TCB *pTcb;
    int prio = 0;

    TEST_START(0);

    pTcb = taskTcb(taskIdSelf());
    TEST_ASSERT(pTcb != NULL);

#ifdef VXWORKS
    pstackBase = (char *) malloc(stackSize) + stacksize;
#endif
  
    TEST_ASSERT(taskInit(&peerTcb,
			 "peerTask",
			 -1,
			 0,
			 pstackBase,
			 stackSize,
			 peerTask,
			 0,0,0,0,0,0,0,0,0,0) == ERROR &&
		errno == S_taskLib_ILLEGAL_PRIORITY);

    TEST_ASSERT_OK(taskInit(&peerTcb,
			    "peerTask",
			    19,
			    0,
                            pstackBase,
			    stackSize,
			    peerTask,
			    0,0,0,0,0,0,0,0,0,0));

    TEST_ASSERT_OK(taskPrioritySet(taskIdSelf(),20));

    TEST_MARK();

    TEST_ASSERT(taskPriorityGet(taskIdSelf(),&prio) == OK && prio == 20);

    TEST_MARK();

    TEST_ASSERT(taskIdVerify(0) == ERROR && errno == S_objLib_OBJ_ID_ERROR);

    TEST_ASSERT_OK(taskIdVerify((TASK_ID)&peerTcb));

    TEST_ASSERT_OK(taskActivate((TASK_ID)&peerTcb));

    TEST_MARK();

    TEST_CHECK_SEQUENCE(SEQ("root",2),
			SEQ("peerTask",2),
			SEQ("root",1),
			END_SEQ);

    TEST_ASSERT_OK(taskDelete((TASK_ID)&peerTcb));

    TEST_ASSERT(taskIdVerify((TASK_ID)&peerTcb)==ERROR);

    TEST_FINISH();
}

int __xeno_user_init (void)
{
    return !taskSpawn("root",
                     0,
                     0,
                     32768,
                     rootTask,
                     0,0,0,0,0,0,0,0,0,0);
}

void __xeno_user_exit (void)
{
}

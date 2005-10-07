/*
 * Copyright (C) 2005 Marco Cavallini (www.KoanSoftware.com)
 *
 * pSOS and pSOS+ are registered trademarks of Wind River Systems, Inc.
 * vxWorks is a registered trademark of Wind River Systems, Inc.
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
 */

#include <vxworks/vxworks.h>
#include <native/timer.h>

#define CLK_RATE 50

static RTIME  st_rOldTempo = 0L ;
static SEM_ID gplcSem ;
static int IdTasktTest ;

void TaskTest(void)
{
	RTIME rTempo ;
	long lTempo ;
	
	printf("Entering TaskTest\n") ;
	
	while (1) 
	{
		rTempo = rt_timer_tsc() ;
		if (st_rOldTempo == 0L) /* first time */
			st_rOldTempo = rTempo ;
		lTempo = rt_timer_tsc2ns(rTempo - st_rOldTempo);
		printf("Time = %ld ms. (%ld ns.)\n", lTempo / 1000000, lTempo);
		st_rOldTempo = rTempo ;

		semTake( gplcSem, WAIT_FOREVER);
	}
}

int CreateTask(void)
{
    IdTasktTest = taskSpawn
            (
                "tTest",            /* name of new task (stored at pStackBase)      */
                45,                /* priority of new task                         */
                VX_FP_TASK,        /* task option word                             */
                0x5000,            /* size (bytes) of stack needed plus name       */
                (FUNCPTR)TaskTest,  /* entry point of new task                   */
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0
            );

    if (IdTasktTest==ERROR) {
        printf("Error in taskSpawn on tTest \n");
        return ERROR;
    }

    return OK;
}

/* Simulate vxWorks usrClock function */
void usrClock(void)
{
	static unsigned long Counter=0;

	Counter++;

	if (Counter & 0x1)
        tickAnnounce(); /* execute system policies */
	else if (Counter >= CLK_RATE * 2)
	{
		semGive(gplcSem);	/* enable our policy */
		Counter = 0;
	}
}
	
void koan_sysClkInit(void)
{
	/* set up the system timer */
	gplcSem = semBCreate( SEM_Q_FIFO, SEM_EMPTY);
	sysClkConnect((void *) usrClock, 0);      /* connect clock ISR */
	sysClkEnable();                  /* start it */
}

/* This is main() */
int root_thread_init (void)
{
	printf("START ktest\n") ;
	koan_sysClkInit() ;
	
 	CreateTask();
	
    printf("STOP ktest\n") ;
    	
    return 0;
}

void root_thread_exit (void)
{
    taskDelete(IdTasktTest);
}

/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
 * Copyright (C) 2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * This file satisfies the references within the emulator code
 * mimicking a VxWorks-like API built upon the XENOMAI nanokernel.
 *
 * VxWorks is a registered trademark of Wind River Systems, Inc.
 */

#ifndef _XENO_SKIN_VXWORKS_H
#define _XENO_SKIN_VXWORKS_H

#include <nucleus/xenomai.h>

#define VXWORKS_SKIN_VERSION_STRING  "3"
#define VXWORKS_SKIN_VERSION_CODE    0x00000003
#define VXWORKS_SKIN_MAGIC           0x57494E44

typedef int STATUS;
typedef int BOOL;

#define OK    (0)
#define ERROR (-1)

/* errno values in some functions */
#define WIND_TASK_ERR_BASE  0x00030000
#define WIND_MEM_ERR_BASE   0x00110000
#define WIND_SEM_ERR_BASE   0x00160000
#define WIND_OBJ_ERR_BASE   0x003d0000
#define WIND_MSGQ_ERR_BASE  0x00410000
#define WIND_INT_ERR_BASE   0x00430000

#define S_objLib_OBJ_ID_ERROR                   (WIND_OBJ_ERR_BASE + 0x0001)
#define S_objLib_OBJ_UNAVAILABLE                (WIND_OBJ_ERR_BASE + 0x0002)
#define S_objLib_OBJ_DELETED                    (WIND_OBJ_ERR_BASE + 0x0003)
#define S_objLib_OBJ_TIMEOUT                    (WIND_OBJ_ERR_BASE + 0x0004)

#define S_taskLib_NAME_NOT_FOUND                (WIND_TASK_ERR_BASE + 0x0065)
#define S_taskLib_TASK_HOOK_NOT_FOUND           (WIND_TASK_ERR_BASE + 0x0067)
#define S_taskLib_ILLEGAL_PRIORITY              (WIND_TASK_ERR_BASE + 0x006d)

#define S_taskLib_TASK_HOOK_TABLE_FULL (WIND_TASK_ERR_BASE + 4) /* FIXME */

#define S_semLib_INVALID_STATE                  (WIND_SEM_ERR_BASE + 0x0065)
#define S_semLib_INVALID_OPTION                 (WIND_SEM_ERR_BASE + 0x0066)
#define S_semLib_INVALID_QUEUE_TYPE             (WIND_SEM_ERR_BASE + 0x0067)
#define S_semLib_INVALID_OPERATION              (WIND_SEM_ERR_BASE + 0x0068)

#define S_msgQLib_INVALID_MSG_LENGTH            (WIND_MSGQ_ERR_BASE + 0x0001)
#define S_msgQLib_NON_ZERO_TIMEOUT_AT_INT_LEVEL (WIND_MSGQ_ERR_BASE + 0x0002)
#define S_msgQLib_INVALID_QUEUE_TYPE            (WIND_MSGQ_ERR_BASE + 0x0003)

#define S_intLib_NOT_ISR_CALLABLE               (WIND_INT_ERR_BASE + 0x0001)

#define S_memLib_NOT_ENOUGH_MEMORY              (WIND_MEM_ERR_BASE + 0x0001)


/* defines for basic tasks handling */

/* Task Options: */
/* execute with floating-point coprocessor support. */
#define VX_FP_TASK (0x0008)

/* include private environment support (see envLib). */
#define VX_PRIVATE_ENV (0x0080)

/* do not fill the stack for use by checkStack(). */
#define VX_NO_STACK_FILL (0x0100)

/* do not allow breakpoint debugging. */
#define VX_UNBREAKABLE (0x0002)

#define WIND_TASK_OPTIONS_MASK                                  \
(VX_FP_TASK|VX_PRIVATE_ENV|VX_NO_STACK_FILL|VX_UNBREAKABLE) 


typedef void (*FUNCPTR) (int, int, int, int, int, int, int, int, int, int);



typedef struct wind_task
{
    unsigned int magic;                  /* Magic code - must be first */


    /* The WIND task internal control block (which tends to be
       rather public in pre-6.0 versions of the VxWorks kernel). */

    char *name;
    int flags;
    int status;
    int prio;
    FUNCPTR entry;
    int errorStatus;

    /* Wind4Xeno specific: used by taskLib */
    int auto_delete;

    unsigned long int flow_id;

    int safecnt;
    xnsynch_t safesync;

    xnthread_t threadbase;

#define thread2wind_task(taddr)                                                 \
( (taddr)                                                                       \
  ? ((wind_task_t *)(((char *)taddr) - (int)(&((wind_task_t *)0)->threadbase))) \
  : NULL )

    xnholder_t link;        /* Link in wind_taskq */

#define link2wind_task(laddr)                                           \
((wind_task_t *)(((char *)laddr) - (int)(&((wind_task_t *)0)->link)))

    int arg0;
    int arg1;
    int arg2;
    int arg3;
    int arg4;
    int arg5;
    int arg6;
    int arg7;
    int arg8;
    int arg9;

    /* Wind4Xeno specific: used by message queues */
    char * rcv_buf;             /* A place to save the receive buffer when this
                                   task is pending on a msgQReceive */
    unsigned int rcv_bytes;     /* this is the size passed to msgQReceive */
    

}  WIND_TCB;



#ifdef errno
#undef errno
#endif

#define errno (*wind_current_context_errno())


/* defines for all kinds of semaphores */
#define SEM_Q_FIFO           0x0
#define SEM_Q_PRIORITY       0x1
#define SEM_DELETE_SAFE      0x4
#define SEM_INVERSION_SAFE   0x8
#define SEM_OPTION_MASK     (SEM_Q_FIFO|SEM_Q_PRIORITY| \
                             SEM_DELETE_SAFE|SEM_INVERSION_SAFE)

/* timeouts when waiting for semaphores */
#define NO_WAIT (0)
#define WAIT_FOREVER (-1)

#if BITS_PER_LONG == 32
#define __natural_word_type int
#else  /* defaults to long othewise */
#define __natural_word_type long
#endif

typedef __natural_word_type SEM_ID;

/*for binary semaphores */
typedef enum {
    SEM_EMPTY =0,
    SEM_FULL =1
} SEM_B_STATE;

typedef __natural_word_type WDOG_ID;

typedef __natural_word_type MSG_Q_ID;

typedef __natural_word_type TASK_ID;

#undef __natural_word_type

#define MSG_PRI_NORMAL (0)
#define MSG_PRI_URGENT (1)

#define MSG_Q_FIFO (0x00)
#define MSG_Q_PRIORITY (0x01)
#define WIND_MSG_Q_OPTION_MASK (MSG_Q_FIFO|MSG_Q_PRIORITY)

typedef unsigned int UINT;

typedef unsigned long long int ULONG;



#ifdef __cplusplus
extern "C" {
#endif

    int * wind_current_context_errno (void);

    /* functions handling errno: */
    void printErrno(int status);
    
    STATUS errnoSet(int status);

    int errnoGet (void);

    int errnoOfTaskGet(TASK_ID task_id);

    STATUS errnoOfTaskSet(TASK_ID task_id, int status);




    /* functions for tasks handling */
    int taskSpawn ( char * name,
                    int prio,
                    int flags,
                    int stacksize,
                    FUNCPTR entry,
                    int arg0, int arg1, int arg2, int arg3, int arg4,
                    int arg5, int arg6, int arg7, int arg8, int arg9 );

    STATUS taskInit ( WIND_TCB * handle,
                      char * name,
                      int prio,
                      int flags,
                      char * stack __attribute__ ((unused)),
                      int stacksize,
                      FUNCPTR entry,
                      int arg0, int arg1, int arg2, int arg3, int arg4,
                      int arg5, int arg6, int arg7, int arg8, int arg9 );

    STATUS taskActivate(TASK_ID task_id);

    void taskExit(int code);
    
    STATUS taskDelete(TASK_ID task_id);

    STATUS taskDeleteForce(TASK_ID task_id);

    STATUS taskSuspend(TASK_ID task_id);

    STATUS taskResume(TASK_ID task_id);

    STATUS taskRestart(TASK_ID task_id);

    STATUS taskPrioritySet(TASK_ID task_id, int prio);

    STATUS taskPriorityGet(TASK_ID task_id, int * pprio);

    STATUS taskLock(void);

    STATUS taskUnlock(void);

    int taskIdSelf(void);
    
    STATUS taskSafe(void);

    STATUS taskUnsafe(void);
    
    STATUS taskDelay(int ticks);

    STATUS taskIdVerify(TASK_ID task_id);

    WIND_TCB *taskTcb(TASK_ID task_id);




    /* functions for task hooks */
    static inline void taskHookInit(void)
    {
    }
    
    typedef void (*wind_create_hook) (WIND_TCB *);

    STATUS taskCreateHookAdd(wind_create_hook hook);

    STATUS taskCreateHookDelete(wind_create_hook hook);


    typedef void (*wind_switch_hook) (WIND_TCB *, WIND_TCB *);

    STATUS taskSwitchHookAdd(wind_switch_hook hook);

    STATUS taskSwitchHookDelete(wind_switch_hook hook);


    typedef void (*wind_delete_hook) (WIND_TCB *);

    STATUS taskDeleteHookAdd(wind_delete_hook hook);

    STATUS taskDeleteHookDelete(wind_delete_hook hook);




    /* functions for tasks information */
    char *taskName ( TASK_ID task_id );

    int taskNameToId ( char * name );

    int taskIdDefault ( TASK_ID task_id );
    
    BOOL taskIsReady ( TASK_ID task_id );

    BOOL taskIsSuspended ( TASK_ID task_id );
         
    /*Missing:
      taskIdListGet()
      taskOptionsGet()
      taskOptionsSet()
      taskRegsGet()
      taskRegsSet()
    */




    /* functions dealing with all kinds of semaphores */
    STATUS semGive(SEM_ID sem_id);

    STATUS semTake(SEM_ID sem_id, int timeout);

    STATUS semFlush(SEM_ID sem_id);

    STATUS semDelete(SEM_ID sem_id);

    


    /* functions for binary semaphores */
    SEM_ID semBCreate(int flags, SEM_B_STATE state);




    /* functions for mutual-exclusion semaphores */
    SEM_ID semMCreate(int flags);

    /* Missing:
    STATUS semMGiveForce(SEM_ID sem_id);
    */



    /* functions for counting semaphores */
    SEM_ID semCCreate(int flags, int count);



    /* functions for watchdogs */
    typedef void (*wind_timer_t) (int);
    
    WDOG_ID wdCreate (void);

    STATUS wdDelete (WDOG_ID handle);

    STATUS wdStart (WDOG_ID handle, int timeout, wind_timer_t handler, int arg);

    STATUS wdCancel (WDOG_ID handle);



    /* Messages queues */
    MSG_Q_ID msgQCreate( int nb_msgs, int length, int flags );

    STATUS msgQDelete( MSG_Q_ID msg );

    int msgQNumMsgs( MSG_Q_ID msg );

    int msgQReceive( MSG_Q_ID msg,char *buf,UINT bytes,int to );

    STATUS msgQSend( MSG_Q_ID msg,char *buf,UINT bytes,int to,int prio );



    /* functions related to interrupts */
    BOOL intContext (void);
    
    int intCount (void);
    
    int intLevelSet ( int level );

    int intLock(void);

    void intUnlock( int flags );




    /* system timer */
    typedef void (*wind_tick_handler_t) (int);
    
    STATUS sysClkConnect(wind_tick_handler_t routine, int arg );

    void sysClkDisable(void);

    void sysClkEnable(void);

    int sysClkRateGet(void);
    
    STATUS sysClkRateSet(int ticksPerSecond);



    /* system clock */
    void tickAnnounce(void);
    
    ULONG tickGet (void);

    void tickSet (ULONG ticks );



    STATUS kernelTimeSlice ( int ticks );
      
    const char *kernelVersion (void);
    
    
#ifdef __cplusplus
}
#endif


#endif /* !_XENO_SKIN_VXWORKS_H */

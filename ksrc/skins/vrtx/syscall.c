/**
 * @file
 * This file is part of the Xenomai project.
 *
 * @note Copyright (C) 2006 Philippe Gerum <rpm@xenomai.org> 
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

#include <nucleus/shadow.h>
#include <vrtx/defs.h>
#include <vrtx/task.h>
#include <vrtx/syscall.h>

/*
 * By convention, error codes are passed back through the syscall
 * return value:
 * - negative codes stand for internal (i.e. nucleus) errors;
 * - strictly positive values stand for genuine VRTX errors.
 * - zero means success.
 */

static int __muxid;

#if 0
static vrtxtask_t *__vrtx_task_current (struct task_struct *curr)

{
    xnthread_t *thread = xnshadow_thread(curr);

    if (!thread || xnthread_get_magic(thread) != VRTX_SKIN_MAGIC)
	return NULL;

    return thread2vrtxtask(thread); /* Convert TCB pointers. */
}
#endif

/*
 * int __sc_tecreate(struct vrtx_arg_bulk *bulk,
 *                   int *ptid,
 *                   xncompletion_t *completion)
 * bulk = {
 * a1: int tid;
 * a2: int prio;
 * a3: int mode;
 * }
 */

static int __sc_tecreate (struct task_struct *curr, struct pt_regs *regs)

{
    xncompletion_t __user *u_completion;
    struct vrtx_arg_bulk bulk;
    int prio, mode, tid, err;
    vrtxtask_t *task;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(bulk)))
	return -EFAULT;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(tid)))
	return -EFAULT;

    __xn_copy_from_user(curr,&bulk,(void __user *)__xn_reg_arg1(regs),sizeof(bulk));

    /* Suggested task id. */
    tid = bulk.a1;
    /* Task priority. */
    prio = bulk.a2;
    /* Task mode. */
    mode = bulk.a3|0x100;

    /* Completion descriptor our parent thread is pending on. */
    u_completion = (xncompletion_t __user *)__xn_reg_arg3(regs);

    task = xnmalloc(sizeof(*task));
    
    if (!task)
	{
	err = ER_TCB;
	goto done;
	}
	
    tid = sc_tecreate_inner(task,NULL,tid,prio,mode,0,0,NULL,0,&err);

    if (tid < 0)
	{
	if (u_completion)
	    xnshadow_signal_completion(u_completion,err);
	}
    else
	{
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg2(regs),&tid,sizeof(tid));
	err = xnshadow_map(&task->threadbase,u_completion);
	}

    if (err)
	xnfree(task);

 done:

    return err;
}

/*
 * int __sc_tdelete(int tid, int opt)
 */

static int __sc_tdelete (struct task_struct *curr, struct pt_regs *regs)

{
    int err, tid, opt;

    tid = __xn_reg_arg1(regs);
    opt = __xn_reg_arg2(regs);
    sc_tdelete(tid,opt,&err);

    return err;
}

static xnsysent_t __systab[] = {
    [__vrtx_tecreate ] = { &__sc_tecreate, __xn_exec_init },
    [__vrtx_tdelete ] = { &__sc_tdelete, __xn_exec_conforming },
};

static void __shadow_delete_hook (xnthread_t *thread)

{
    if (xnthread_get_magic(thread) == VRTX_SKIN_MAGIC &&
	testbits(thread->status,XNSHADOW))
	xnshadow_unmap(thread);
}

int vrtxsys_init (void)

{
    __muxid =
	xnshadow_register_interface("vrtx",
				    VRTX_SKIN_MAGIC,
				    sizeof(__systab) / sizeof(__systab[0]),
				    __systab,
				    NULL);
    if (__muxid < 0)
	return -ENOSYS;

    xnpod_add_hook(XNHOOK_THREAD_DELETE,&__shadow_delete_hook);
    
    return 0;
}

void vrtxsys_cleanup (void)

{
    xnpod_remove_hook(XNHOOK_THREAD_DELETE,&__shadow_delete_hook);
    xnshadow_unregister_interface(__muxid);
}

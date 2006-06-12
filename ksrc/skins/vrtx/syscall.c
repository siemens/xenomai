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
static vrtxtask_t *__vrtx_task_current(struct task_struct *curr)
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

static int __sc_tecreate(struct task_struct *curr, struct pt_regs *regs)
{
    xncompletion_t __user *u_completion;
    struct vrtx_arg_bulk bulk;
    int prio, mode, tid, err;
    vrtxtask_t *task;

    if (!__xn_access_ok(curr, VERIFY_READ, __xn_reg_arg1(regs), sizeof(bulk)))
        return -EFAULT;

    if (!__xn_access_ok(curr, VERIFY_WRITE, __xn_reg_arg2(regs), sizeof(tid)))
        return -EFAULT;

    __xn_copy_from_user(curr, &bulk, (void __user *)__xn_reg_arg1(regs),
                        sizeof(bulk));

    /* Suggested task id. */
    tid = bulk.a1;
    /* Task priority. */
    prio = bulk.a2;
    /* Task mode. */
    mode = bulk.a3 | 0x100;

    /* Completion descriptor our parent thread is pending on. */
    u_completion = (xncompletion_t __user *)__xn_reg_arg3(regs);

    task = xnmalloc(sizeof(*task));

    if (!task) {
        err = ER_TCB;
        goto done;
    }

    tid = sc_tecreate_inner(task, NULL, tid, prio, mode, 0, 0, NULL, 0, &err);

    if (tid < 0) {
        if (u_completion)
            xnshadow_signal_completion(u_completion, err);
    } else {
        __xn_copy_to_user(curr, (void __user *)__xn_reg_arg2(regs), &tid,
                          sizeof(tid));
        err = xnshadow_map(&task->threadbase, u_completion);
    }

    if (err)
        xnfree(task);

done:

    return err;
}

/*
 * int __sc_tdelete(int tid, int opt)
 */

static int __sc_tdelete(struct task_struct *curr, struct pt_regs *regs)
{
    int err, tid, opt;

    tid = __xn_reg_arg1(regs);
    opt = __xn_reg_arg2(regs);
    sc_tdelete(tid, opt, &err);

    return err;
}

/*
 * int __sc_tpriority(int tid, int prio)
 */

static int __sc_tpriority(struct task_struct *curr, struct pt_regs *regs)
{
    int err, tid, prio;

    tid = __xn_reg_arg1(regs);
    prio = __xn_reg_arg2(regs);
    sc_tpriority(tid, prio, &err);

    return err;
}

/*
 * int __sc_tresume(int tid, int opt)
 */

static int __sc_tresume(struct task_struct *curr, struct pt_regs *regs)
{
    int err, tid, opt;

    tid = __xn_reg_arg1(regs);
    opt = __xn_reg_arg2(regs);
    sc_tpriority(tid, opt, &err);

    return err;
}

/*
 * int __sc_tsuspend(int tid, int opt)
 */

static int __sc_tsuspend(struct task_struct *curr, struct pt_regs *regs)
{
    int err, tid, opt;

    tid = __xn_reg_arg1(regs);
    opt = __xn_reg_arg2(regs);
    sc_tsuspend(tid, opt, &err);

    return err;
}

/*
 * int __sc_tslice(unsigned short ticks)
 */

static int __sc_tslice(struct task_struct *curr, struct pt_regs *regs)
{
    unsigned short ticks;

    ticks = (unsigned short)__xn_reg_arg1(regs);
    sc_tslice(ticks);

    return 0;
}

/*
 * int __sc_tinquiry(int pinfo[], TCB *tcb, int tid)
 */

static int __sc_tinquiry(struct task_struct *curr, struct pt_regs *regs)
{
    int err, tid, pinfo[3];
    TCB *tcb;

    if (!__xn_access_ok(curr, VERIFY_WRITE, __xn_reg_arg1(regs), sizeof(pinfo)))
        return -EFAULT;

    if (!__xn_access_ok(curr, VERIFY_WRITE, __xn_reg_arg2(regs), sizeof(*tcb)))
        return -EFAULT;

    tid = __xn_reg_arg3(regs);
    tcb = sc_tinquiry(pinfo, tid, &err);

    if (!err) {
        __xn_copy_to_user(curr, (void __user *)__xn_reg_arg1(regs), pinfo,
                          sizeof(pinfo));
        __xn_copy_to_user(curr, (void __user *)__xn_reg_arg2(regs), tcb,
                          sizeof(*tcb));
    }

    return err;
}

/*
 * int __sc_lock(void)
 */

static int __sc_lock(struct task_struct *curr, struct pt_regs *regs)
{
    sc_lock();
    return 0;
}

/*
 * int __sc_unlock(void)
 */

static int __sc_unlock(struct task_struct *curr, struct pt_regs *regs)
{
    sc_unlock();
    return 0;
}

/*
 * int __sc_delay(long timeout)
 */

static int __sc_delay(struct task_struct *curr, struct pt_regs *regs)
{
    sc_delay(__xn_reg_arg1(regs));
    return 0;
}

/*
 * int __sc_adelay(struct timespec *time)
 */

static int __sc_adelay(struct task_struct *curr, struct pt_regs *regs)
{
    struct timespec time;
    int err;

    if (!__xn_access_ok(curr, VERIFY_READ, __xn_reg_arg1(regs), sizeof(time)))
        return -EFAULT;

    __xn_copy_from_user(curr, &time, (void __user *)__xn_reg_arg1(regs),
                        sizeof(time));

    sc_adelay(time,&err);

    return err;
}

/*
 * int __sc_stime(unsigned long ticks)
 */

static int __sc_stime(struct task_struct *curr, struct pt_regs *regs)
{
    sc_stime(__xn_reg_arg1(regs));
    return 0;
}

/*
 * int __sc_gtime(unsigned long *ticks_p)
 */

static int __sc_gtime(struct task_struct *curr, struct pt_regs *regs)
{
	unsigned long ticks;

    if (!__xn_access_ok(curr, VERIFY_WRITE, __xn_reg_arg1(regs), sizeof(ticks)))
        return -EFAULT;

    ticks = sc_gtime();

	__xn_copy_to_user(curr, (void __user *)__xn_reg_arg1(regs), &ticks,
					  sizeof(ticks));
    return 0;
}

/*
 * int __sc_sclock(struct timespec *time, unsigned long ns)
 */

static int __sc_sclock(struct task_struct *curr, struct pt_regs *regs)
{
    struct timespec time;
	unsigned long ns;
	int err;

    if (!__xn_access_ok(curr, VERIFY_READ, __xn_reg_arg1(regs), sizeof(time)))
        return -EFAULT;

    __xn_copy_from_user(curr, &time, (void __user *)__xn_reg_arg1(regs),
                        sizeof(time));

	ns = __xn_reg_arg1(regs);

	sc_sclock(time,ns,&err);

    return err;
}

/*
 * int __sc_gclock(struct timespec *time, unsigned long ns)
 */

static int __sc_gclock(struct task_struct *curr, struct pt_regs *regs)
{
    struct timespec time;
	unsigned long ns;
	int err;

    if (!__xn_access_ok(curr, VERIFY_WRITE, __xn_reg_arg1(regs), sizeof(time)))
        return -EFAULT;

    if (!__xn_access_ok(curr, VERIFY_WRITE, __xn_reg_arg2(regs), sizeof(ns)))
        return -EFAULT;

	sc_gclock(&time,&ns,&err);

	if (!err) {
		__xn_copy_to_user(curr, (void __user *)__xn_reg_arg1(regs), &time,
						  sizeof(time));
		__xn_copy_to_user(curr, (void __user *)__xn_reg_arg2(regs), &ns,
						  sizeof(ns));
	}

    return err;
}

/*
 * int __sc_mcreate(int opt, int *mid)
 */

static int __sc_mcreate(struct task_struct *curr, struct pt_regs *regs)
{
	int opt = __xn_reg_arg1(regs), mid, err;

    if (!__xn_access_ok(curr, VERIFY_WRITE, __xn_reg_arg2(regs), sizeof(mid)))
        return -EFAULT;

	mid = sc_mcreate(opt,&err);

	if (!err)
		__xn_copy_to_user(curr, (void __user *)__xn_reg_arg2(regs), &mid,
						  sizeof(mid));
	return err;
}

/*
 * int __sc_mdelete(int mid, int opt)
 */

static int __sc_mdelete(struct task_struct *curr, struct pt_regs *regs)
{
	int opt, mid, err;

	mid = __xn_reg_arg1(regs);
	opt = __xn_reg_arg2(regs);
	sc_mdelete(mid,opt,&err);

	return err;
}

/*
 * int __sc_mpost(int mid)
 */

static int __sc_mpost(struct task_struct *curr, struct pt_regs *regs)
{
	int mid, err;

	mid = __xn_reg_arg1(regs);
	sc_mpost(mid,&err);

	return err;
}

/*
 * int __sc_maccept(int mid)
 */

static int __sc_maccept(struct task_struct *curr, struct pt_regs *regs)
{
	int mid, err;

	mid = __xn_reg_arg1(regs);
	sc_maccept(mid,&err);

	return err;
}

/*
 * int __sc_mpend(int mid, unsigned long timeout)
 */

static int __sc_mpend(struct task_struct *curr, struct pt_regs *regs)
{
	unsigned long timeout;
	int mid, err;

	mid = __xn_reg_arg1(regs);
	timeout = __xn_reg_arg2(regs);
	sc_mpend(mid,timeout,&err);

	return err;
}

/*
 * int __sc_minquiry(int mid, int *statusp)
 */

static int __sc_minquiry(struct task_struct *curr, struct pt_regs *regs)
{
	int mid, status, err;

    if (!__xn_access_ok(curr, VERIFY_WRITE, __xn_reg_arg2(regs), sizeof(status)))
        return -EFAULT;

	mid = __xn_reg_arg1(regs);
	status = sc_minquiry(mid,&err);

	if (!err)
		__xn_copy_to_user(curr, (void __user *)__xn_reg_arg2(regs), &status,
						  sizeof(status));
	return err;
}

/*
 * int __sc_qecreate(int qid, int qsize, int opt, int *qidp)
 */

static int __sc_qecreate(struct task_struct *curr, struct pt_regs *regs)
{
    int qid, qsize, opt, err;

    if (!__xn_access_ok(curr, VERIFY_WRITE, __xn_reg_arg4(regs), sizeof(qid)))
        return -EFAULT;

    qid = __xn_reg_arg1(regs);
    qsize = __xn_reg_arg2(regs);
    opt = __xn_reg_arg3(regs);
    qid = sc_qecreate(qid,qsize,opt,&err);

	if (!err)
		__xn_copy_to_user(curr, (void __user *)__xn_reg_arg4(regs), &qid,
						  sizeof(qid));
	return err;
}

/*
 * int __sc_qdelete(int qid, int opt)
 */

static int __sc_qdelete(struct task_struct *curr, struct pt_regs *regs)
{
    int qid, opt, err;

    qid = __xn_reg_arg1(regs);
    opt = __xn_reg_arg2(regs);
    sc_qdelete(qid,opt,&err);

	return err;
}

/*
 * int __sc_qpost(int qid, char *msg)
 */

static int __sc_qpost(struct task_struct *curr, struct pt_regs *regs)
{
    int qid, err;
	char *msg;

    qid = __xn_reg_arg1(regs);
    msg = (char *)__xn_reg_arg2(regs);
    sc_qpost(qid,msg,&err);

	return err;
}

/*
 * int __sc_qbrdcst(int qid, char *msg)
 */

static int __sc_qbrdcst(struct task_struct *curr, struct pt_regs *regs)
{
    int qid, err;
	char *msg;

    qid = __xn_reg_arg1(regs);
    msg = (char *)__xn_reg_arg2(regs);
    sc_qbrdcst(qid,msg,&err);

	return err;
}

/*
 * int __sc_qjam(int qid, char *msg)
 */

static int __sc_qjam(struct task_struct *curr, struct pt_regs *regs)
{
    int qid, err;
	char *msg;

    qid = __xn_reg_arg1(regs);
    msg = (char *)__xn_reg_arg2(regs);
    sc_qjam(qid,msg,&err);

	return err;
}

/*
 * int __sc_qpend(int qid, unsigned long timeout, char **msgp)
 */

static int __sc_qpend(struct task_struct *curr, struct pt_regs *regs)
{
	long timeout;
    int qid, err;
	char *msg;

    if (!__xn_access_ok(curr, VERIFY_WRITE, __xn_reg_arg3(regs), sizeof(msg)))
        return -EFAULT;

    qid = __xn_reg_arg1(regs);
    timeout = __xn_reg_arg2(regs);
    msg = sc_qpend(qid,timeout,&err);

	if (!err)
		__xn_copy_to_user(curr, (void __user *)__xn_reg_arg3(regs), &msg,
						  sizeof(msg));
	return err;
}

/*
 * int __sc_qaccept(int qid, char **msgp)
 */

static int __sc_qaccept(struct task_struct *curr, struct pt_regs *regs)
{
    int qid, err;
	char *msg;

    if (!__xn_access_ok(curr, VERIFY_WRITE, __xn_reg_arg2(regs), sizeof(msg)))
        return -EFAULT;

    qid = __xn_reg_arg1(regs);
    msg = sc_qaccept(qid,&err);

	if (!err)
		__xn_copy_to_user(curr, (void __user *)__xn_reg_arg2(regs), &msg,
						  sizeof(msg));
	return err;
}

/*
 * int __sc_qinquiry(int qid, int *countp, char **msgp)
 */

static int __sc_qinquiry(struct task_struct *curr, struct pt_regs *regs)
{
    int qid, count, err;
	char *msg;

    if (!__xn_access_ok(curr, VERIFY_WRITE, __xn_reg_arg2(regs), sizeof(count)))
        return -EFAULT;

    if (!__xn_access_ok(curr, VERIFY_WRITE, __xn_reg_arg3(regs), sizeof(msg)))
        return -EFAULT;

    qid = __xn_reg_arg1(regs);
    msg = sc_qinquiry(qid,&count,&err);

	if (!err) {
		__xn_copy_to_user(curr, (void __user *)__xn_reg_arg2(regs), &count,
						  sizeof(count));
		__xn_copy_to_user(curr, (void __user *)__xn_reg_arg3(regs), &msg,
						  sizeof(msg));
	}

	return err;
}

/*
 * int __sc_post(char **mboxp, char *msg)
 */

static int __sc_post(struct task_struct *curr, struct pt_regs *regs)
{
	char **mboxp, *msg;
	int err;

	/* We should be able to write to a mailbox storage, even if we
	 * actually don't. */
    if (!__xn_access_ok(curr, VERIFY_WRITE, __xn_reg_arg1(regs), sizeof(msg)))
        return -EFAULT;

    mboxp = (char **)__xn_reg_arg1(regs);
    msg = (char *)__xn_reg_arg2(regs);
	sc_post(mboxp,msg,&err);

	return err;
}

/*
 * int __sc_accept(char **mboxp, char **msgp)
 */

static int __sc_accept(struct task_struct *curr, struct pt_regs *regs)
{
	char **mboxp, *msg;
	int err;

	/* We should be able to write to a mailbox storage, even if we
	 * actually don't. */
    if (!__xn_access_ok(curr, VERIFY_WRITE, __xn_reg_arg1(regs), sizeof(msg)))
        return -EFAULT;

    if (!__xn_access_ok(curr, VERIFY_WRITE, __xn_reg_arg2(regs), sizeof(msg)))
        return -EFAULT;

    mboxp = (char **)__xn_reg_arg1(regs);
	msg = sc_accept(mboxp,&err);

	if (!err)
		__xn_copy_to_user(curr, (void __user *)__xn_reg_arg2(regs), &msg,
						  sizeof(msg));
	return err;
}

/*
 * int __sc_pend(char **mboxp, long timeout, char **msgp)
 */

static int __sc_pend(struct task_struct *curr, struct pt_regs *regs)
{
	char **mboxp, *msg;
	long timeout;
	int err;

	/* We should be able to write to a mailbox storage, even if we
	 * actually don't. */
    if (!__xn_access_ok(curr, VERIFY_WRITE, __xn_reg_arg1(regs), sizeof(msg)))
        return -EFAULT;

    if (!__xn_access_ok(curr, VERIFY_WRITE, __xn_reg_arg3(regs), sizeof(msg)))
        return -EFAULT;

    mboxp = (char **)__xn_reg_arg1(regs);
    timeout = __xn_reg_arg2(regs);
	msg = sc_pend(mboxp,timeout,&err);

	if (!err)
		__xn_copy_to_user(curr, (void __user *)__xn_reg_arg3(regs), &msg,
						  sizeof(msg));
	return err;
}

static xnsysent_t __systab[] = {
    [__vrtx_tecreate] = {&__sc_tecreate, __xn_exec_init},
    [__vrtx_tdelete] = {&__sc_tdelete, __xn_exec_conforming},
    [__vrtx_tpriority] = {&__sc_tpriority, __xn_exec_primary},
    [__vrtx_tresume] = {&__sc_tresume, __xn_exec_primary},
    [__vrtx_tsuspend] = {&__sc_tsuspend, __xn_exec_primary},
    [__vrtx_tslice] = {&__sc_tslice, __xn_exec_any},
    [__vrtx_tinquiry] = {&__sc_tinquiry, __xn_exec_primary},
    [__vrtx_lock] = {&__sc_lock, __xn_exec_primary},
    [__vrtx_unlock] = {&__sc_unlock, __xn_exec_primary},
    [__vrtx_delay] = {&__sc_delay, __xn_exec_primary},
    [__vrtx_adelay] = {&__sc_adelay, __xn_exec_primary},
    [__vrtx_stime] = {&__sc_stime, __xn_exec_any},
    [__vrtx_gtime] = {&__sc_gtime, __xn_exec_any},
    [__vrtx_sclock] = {&__sc_sclock, __xn_exec_any},
    [__vrtx_gclock] = {&__sc_gclock, __xn_exec_any},
    [__vrtx_mcreate] = {&__sc_mcreate, __xn_exec_any},
    [__vrtx_mdelete] = {&__sc_mdelete, __xn_exec_any},
    [__vrtx_mpost] = {&__sc_mpost, __xn_exec_primary},
    [__vrtx_maccept] = {&__sc_maccept, __xn_exec_primary},
    [__vrtx_mpend] = {&__sc_mpend, __xn_exec_primary},
    [__vrtx_minquiry] = {&__sc_minquiry, __xn_exec_any},
    [__vrtx_qecreate] = {&__sc_qecreate, __xn_exec_any},
    [__vrtx_qdelete] = {&__sc_qdelete, __xn_exec_any},
    [__vrtx_qpost] = {&__sc_qpost, __xn_exec_any},
    [__vrtx_qbrdcst] = {&__sc_qbrdcst, __xn_exec_any},
    [__vrtx_qjam] = {&__sc_qjam, __xn_exec_any},
    [__vrtx_qpend] = {&__sc_qpend, __xn_exec_primary},
    [__vrtx_qaccept] = {&__sc_qaccept, __xn_exec_any},
    [__vrtx_qinquiry] = {&__sc_qinquiry, __xn_exec_any},
    [__vrtx_post] = {&__sc_post, __xn_exec_any},
    [__vrtx_accept] = {&__sc_accept, __xn_exec_any},
    [__vrtx_pend] = {&__sc_pend, __xn_exec_primary},
};

static void __shadow_delete_hook(xnthread_t *thread)
{
    if (xnthread_get_magic(thread) == VRTX_SKIN_MAGIC &&
        testbits(thread->status, XNSHADOW))
        xnshadow_unmap(thread);
}

int vrtxsys_init(void)
{
    __muxid =
        xnshadow_register_interface("vrtx",
                                    VRTX_SKIN_MAGIC,
                                    sizeof(__systab) / sizeof(__systab[0]),
                                    __systab, NULL);
    if (__muxid < 0)
        return -ENOSYS;

    xnpod_add_hook(XNHOOK_THREAD_DELETE, &__shadow_delete_hook);

    return 0;
}

void vrtxsys_cleanup(void)
{
    xnpod_remove_hook(XNHOOK_THREAD_DELETE, &__shadow_delete_hook);
    xnshadow_unregister_interface(__muxid);
}

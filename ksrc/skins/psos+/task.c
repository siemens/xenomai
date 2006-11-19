/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <nucleus/registry.h>
#include <psos+/task.h>
#include <psos+/tm.h>

static xnqueue_t psostaskq;

static u_long psos_time_slice;

static u_long psos_task_ids;

static void psostask_delete_hook(xnthread_t *thread)
{
	/* The scheduler is locked while hooks are running */
	psostask_t *task;
	psostm_t *tm;

	if (xnthread_get_magic(thread) != PSOS_SKIN_MAGIC)
		return;

#ifdef CONFIG_XENO_OPT_REGISTRY
	if (xnthread_handle(thread) != XN_NO_HANDLE)
	    xnregistry_remove(xnthread_handle(thread));
#endif /* CONFIG_XENO_OPT_REGISTRY */

	task = thread2psostask(thread);

	removeq(&psostaskq, &task->link);

	while ((tm = (psostm_t *)getgq(&task->alarmq)) != NULL)
		tm_destroy_internal(tm);

	ev_destroy(&task->evgroup);
	xnarch_delete_display(&task->threadbase);
	psos_mark_deleted(task);

	xnfreesafe(&task->threadbase, task, &task->link);
}

void psostask_init(u_long rrperiod)
{
	initq(&psostaskq);
	psos_time_slice = rrperiod;
	xnpod_add_hook(XNHOOK_THREAD_DELETE, psostask_delete_hook);
}

void psostask_cleanup(void)
{
	xnholder_t *holder;

	while ((holder = getheadq(&psostaskq)) != NULL)
		t_delete((u_long)link2psostask(holder));

	xnpod_remove_hook(XNHOOK_THREAD_DELETE, psostask_delete_hook);
}

u_long t_create(char name[4],
		u_long prio,
		u_long sstack, u_long ustack, u_long flags, u_long *tid_r)
{
	xnflags_t bflags = 0;
	psostask_t *task;
	char aname[5];
	u_long err;
	spl_t s;
	int n;

	if (prio < 1 || prio > 255)
		return ERR_PRIOR;

	aname[0] = name[0];
	aname[1] = name[1];
	aname[2] = name[2];
	aname[3] = name[3];
	aname[4] = '\0';

	task = (psostask_t *)xnmalloc(sizeof(*task));

	if (!task)
		return ERR_NOTCB;

	if (flags & T_FPU)
	    bflags |= XNFPU;

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
	if (flags & T_SHADOW)
		bflags |= XNSHADOW;
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */

	ustack += sstack;

	if (!(flags & T_SHADOW) && ustack < 1024) {
		xnfree(task);
		return ERR_TINYSTK;
	}

	if (*aname)
		xnobject_copy_name(task->name, aname);
	else
		/* i.e. Anonymous object which must be accessible from
		   user-space. */
		sprintf(task->name, "anon%lu", psos_task_ids++);

	if (xnpod_init_thread(&task->threadbase,
			      task->name, prio, bflags, ustack) != 0) {
		xnfree(task);
		return ERR_NOSTK;	/* Assume this is the only possible failure */
	}

	xnthread_set_magic(&task->threadbase, PSOS_SKIN_MAGIC);
	xnthread_time_slice(&task->threadbase) = psos_time_slice;

	ev_init(&task->evgroup);
	inith(&task->link);

	for (n = 0; n < PSOSTASK_NOTEPAD_REGS; n++)
		task->notepad[n] = 0;

	initgq(&task->alarmq,
	       &xnmod_glink_queue,
	       xnmod_alloc_glinks,
	       XNMOD_GHOLDER_THRESHOLD,
	       xnpod_get_qdir(nkpod), xnpod_get_maxprio(nkpod, 0));

	task->magic = PSOS_TASK_MAGIC;

	xnlock_get_irqsave(&nklock, s);
	appendq(&psostaskq, &task->link);
	*tid_r = (u_long)task;
	xnlock_put_irqrestore(&nklock, s);

#ifdef CONFIG_XENO_OPT_REGISTRY
	err = xnregistry_enter(task->name,
			       task, &xnthread_handle(&task->threadbase), NULL);
	if (err) {
		t_delete((u_long)task);
		return err;
	}
#endif /* CONFIG_XENO_OPT_REGISTRY */

	xnarch_create_display(&task->threadbase, aname, psostask);

	return SUCCESS;
}

static void psostask_trampoline(void *cookie)
{

	psostask_t *task = (psostask_t *)cookie;
	task->entry(task->args[0], task->args[1], task->args[2], task->args[3]);
	t_delete(0);
}

u_long t_start(u_long tid,
	       u_long mode,
	       void (*startaddr) (u_long, u_long, u_long, u_long),
	       u_long targs[])
{
	u_long err = SUCCESS;
	xnflags_t xnmode;
	psostask_t *task;
	spl_t s;
	int n;

	xnlock_get_irqsave(&nklock, s);

	task = psos_h2obj_active(tid, PSOS_TASK_MAGIC, psostask_t);

	if (!task) {
		err = psos_handle_error(tid, PSOS_TASK_MAGIC, psostask_t);
		goto unlock_and_exit;
	}

	if (!xnthread_test_flags(&task->threadbase, XNDORMANT)) {
		err = ERR_ACTIVE;	/* Task already started */
		goto unlock_and_exit;
	}

	xnmode = psos_mode_to_xeno(mode);

	for (n = 0; n < 4; n++)
		task->args[n] = targs ? targs[n] : 0;

	task->entry = startaddr;

	xnpod_start_thread(&task->threadbase,
			   xnmode,
			   (int)((mode >> 8) & 0x7),
			   XNPOD_ALL_CPUS, psostask_trampoline, task);

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

u_long t_restart(u_long tid, u_long targs[])
{
	u_long err = SUCCESS;
	psostask_t *task;
	spl_t s;
	int n;

	if (xnpod_unblockable_p())
		return -EPERM;

	xnlock_get_irqsave(&nklock, s);

	if (tid == 0)
		task = psos_current_task();
	else {
		task = psos_h2obj_active(tid, PSOS_TASK_MAGIC, psostask_t);

		if (!task) {
			err = psos_handle_error(tid, PSOS_TASK_MAGIC, psostask_t);
			goto unlock_and_exit;
		}

		if (xnthread_test_flags(&task->threadbase, XNDORMANT)) {
			err = ERR_NACTIVE;
			goto unlock_and_exit;
		}
	}

	for (n = 0; n < 4; n++)
		task->args[n] = targs ? targs[n] : 0;

	xnpod_restart_thread(&task->threadbase);

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

u_long t_delete(u_long tid)
{
	u_long err = SUCCESS;
	psostask_t *task;
	spl_t s;

	if (tid == 0)
		xnpod_delete_self();	/* Never returns */

	xnlock_get_irqsave(&nklock, s);

	task = psos_h2obj_active(tid, PSOS_TASK_MAGIC, psostask_t);

	if (!task) {
		err = psos_handle_error(tid, PSOS_TASK_MAGIC, psostask_t);
		goto unlock_and_exit;
	}

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
	if (xnthread_user_task(&task->threadbase) != NULL
	    && !xnthread_test_flags(&task->threadbase,XNDORMANT)
	    && (!xnpod_primary_p() || task != psos_current_task()))
		xnshadow_send_sig(&task->threadbase, SIGKILL, 1);
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */

	xnpod_delete_thread(&task->threadbase);

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

u_long t_ident(char name[4], u_long node, u_long *tid_r)
{
	u_long err = SUCCESS;
	xnholder_t *holder;
	psostask_t *task;
	spl_t s;

	if (node > 1)
		return ERR_NODENO;

	if (!name) {
		if (xnpod_unblockable_p())
			return ERR_OBJID;
		*tid_r = (u_long)psos_current_task();
		return SUCCESS;
	}

	xnlock_get_irqsave(&nklock, s);

	for (holder = getheadq(&psostaskq);
	     holder; holder = nextq(&psostaskq, holder)) {
		task = link2psostask(holder);

		if (task->threadbase.name[0] == name[0] &&
		    task->threadbase.name[1] == name[1] &&
		    task->threadbase.name[2] == name[2] &&
		    task->threadbase.name[3] == name[3]) {
			*tid_r = (u_long)task;
			goto unlock_and_exit;
		}
	}

	err = ERR_OBJNF;

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

u_long t_mode(u_long clrmask, u_long setmask, u_long *oldmode)
{
	psostask_t *task;

	if (xnpod_unblockable_p())
		return -EPERM;

	task = psos_current_task();

	*oldmode =
	    xeno_mode_to_psos(xnpod_set_thread_mode
			      (&task->threadbase,
			       psos_mode_to_xeno(clrmask),
			       psos_mode_to_xeno(setmask)));
	*oldmode |= ((task->threadbase.imask & 0x7) << 8);

	/* Reschedule in case the scheduler has been unlocked. */
	xnpod_schedule();

	return SUCCESS;
}

u_long t_getreg(u_long tid, u_long regnum, u_long *regvalue)
{
	u_long err = SUCCESS;
	psostask_t *task;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (tid == 0)
		task = psos_current_task();
	else {
		task = psos_h2obj_active(tid, PSOS_TASK_MAGIC, psostask_t);

		if (!task) {
			err = psos_handle_error(tid, PSOS_TASK_MAGIC, psostask_t);
			goto unlock_and_exit;
		}
	}

	if (regnum >= PSOSTASK_NOTEPAD_REGS) {
		err = ERR_REGNUM;
		goto unlock_and_exit;
	}

	*regvalue = task->notepad[regnum];

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

u_long t_resume(u_long tid)
{
	u_long err = SUCCESS;
	psostask_t *task;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (tid == 0) {
		if (xnpod_unblockable_p()) {
			err = -EPERM;
			goto unlock_and_exit;
		}

		/* Would be admittedly silly, but silly code does
		 * exist, and it's a matter of returning ERR_NOTSUSP
		 * instead of ERR_OBJID. */
		task = psos_current_task();
	}
	else {
		task = psos_h2obj_active(tid, PSOS_TASK_MAGIC, psostask_t);

		if (!task) {
			err = psos_handle_error(tid, PSOS_TASK_MAGIC, psostask_t);
			goto unlock_and_exit;
		}
	}

	if (!xnthread_test_flags(&task->threadbase, XNSUSP)) {
		err = ERR_NOTSUSP;	/* Task not suspended. */
		goto unlock_and_exit;
	}

	xnpod_resume_thread(&task->threadbase, XNSUSP);
	xnpod_schedule();

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

u_long t_suspend(u_long tid)
{
	u_long err = SUCCESS;
	psostask_t *task;
	spl_t s;

	if (xnpod_unblockable_p())
		return -EPERM;

	if (tid == 0) {
		xnpod_suspend_self();
		return SUCCESS;
	}

	xnlock_get_irqsave(&nklock, s);

	task = psos_h2obj_active(tid, PSOS_TASK_MAGIC, psostask_t);

	if (!task) {
		err = psos_handle_error(tid, PSOS_TASK_MAGIC, psostask_t);
		goto unlock_and_exit;
	}

	if (xnthread_test_flags(&task->threadbase, XNSUSP)) {
		err = ERR_SUSP;	/* Task already suspended. */
		goto unlock_and_exit;
	}

	xnpod_suspend_thread(&task->threadbase, XNSUSP, XN_INFINITE, NULL);

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

u_long t_setpri(u_long tid, u_long newprio, u_long *oldprio)
{
	u_long err = SUCCESS;
	psostask_t *task;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (tid == 0) {
		if (xnpod_unblockable_p())
			return -EPERM;
		task = psos_current_task();
	}
	else {
		task = psos_h2obj_active(tid, PSOS_TASK_MAGIC, psostask_t);

		if (!task) {
			err =
			    psos_handle_error(tid, PSOS_TASK_MAGIC, psostask_t);
			goto unlock_and_exit;
		}
	}

	*oldprio = xnthread_current_priority(&task->threadbase);

	if (newprio != 0) {
		if (newprio < 1 || newprio > 255) {
			err = ERR_SETPRI;
			goto unlock_and_exit;
		}

		if (newprio != *oldprio) {
			xnpod_renice_thread(&task->threadbase, newprio);
			xnpod_schedule();
		}
	}

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

u_long t_setreg(u_long tid, u_long regnum, u_long regvalue)
{
	u_long err = SUCCESS;
	psostask_t *task;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (tid == 0)
		task = psos_current_task();
	else {
		task = psos_h2obj_active(tid, PSOS_TASK_MAGIC, psostask_t);

		if (!task) {
			err = psos_handle_error(tid, PSOS_TASK_MAGIC, psostask_t);
			goto unlock_and_exit;
		}
	}

	if (regnum >= PSOSTASK_NOTEPAD_REGS) {
		err = ERR_REGNUM;
		goto unlock_and_exit;
	}

	task->notepad[regnum] = regvalue;

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

/*
 * IMPLEMENTATION NOTES:
 *
 * - Code executing on behalf of interrupt context is currently not
 * allowed to scan/alter the global psos task queue (psostaskq).
 */

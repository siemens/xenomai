#include <nucleus/synch.h>
#include <nucleus/thread.h>
#include <nucleus/trace.h>
#include <rtdm/rttesting.h>
#include <rtdm/rtdm_driver.h>
#include <asm/xenomai/fptest.h>

#define RTSWITCH_RT      0x4
#define RTSWITCH_NRT     0
#define RTSWITCH_KERNEL  0x8

typedef struct {
	struct rttst_swtest_task base;
	rtdm_event_t rt_synch;
	struct semaphore nrt_synch;
	xnthread_t ktask;          /* For kernel-space real-time tasks. */
	unsigned last_switch;
} rtswitch_task_t;

typedef struct rtswitch_context {
	rtswitch_task_t *tasks;
	unsigned tasks_count;
	unsigned next_index;
	struct semaphore lock;
	unsigned cpu;
	unsigned switches_count;

	unsigned long pause_us;
	unsigned next_task;
	rtdm_timer_t wake_up_delay;

	unsigned failed;
	struct rttst_swtest_error error;

	rtswitch_task_t *utask;
	rtdm_nrtsig_t wake_utask;
} rtswitch_context_t;

static unsigned int start_index;

module_param(start_index, uint, 0400);
MODULE_PARM_DESC(start_index, "First device instance number to be used");

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gilles.Chanteperdrix@laposte.net");

static void handle_ktask_error(rtswitch_context_t *ctx, unsigned fp_val)
{
	rtswitch_task_t *cur = &ctx->tasks[ctx->error.last_switch.to];
	unsigned i;

	ctx->failed = 1;
	ctx->error.fp_val = fp_val;

	if ((cur->base.flags & RTSWITCH_RT) == RTSWITCH_RT)
		for (i = 0; i < ctx->tasks_count; i++) {
			rtswitch_task_t *task = &ctx->tasks[i];

			/* Find the first non kernel-space task. */
			if ((task->base.flags & RTSWITCH_KERNEL))
				continue;

			/* Unblock it. */
			switch(task->base.flags & RTSWITCH_RT) {
			case RTSWITCH_NRT:
				ctx->utask = task;
				rtdm_nrtsig_pend(&ctx->wake_utask);
				break;

			case RTSWITCH_RT:
				rtdm_event_signal(&task->rt_synch);
				break;
			}

			xnpod_suspend_self();
		}
}

static int rtswitch_pend_rt(rtswitch_context_t *ctx,
			    unsigned idx)
{
	rtswitch_task_t *task;
	int rc;

	if (idx > ctx->tasks_count)
		return -EINVAL;

	task = &ctx->tasks[idx];
	task->base.flags |= RTSWITCH_RT;

	rc = rtdm_event_wait(&task->rt_synch);
	if (rc < 0)
		return rc;

	if (ctx->failed)
		return 1;

	return 0;
}

static void timed_wake_up(rtdm_timer_t *timer)
{
	rtswitch_context_t *ctx =
		container_of(timer, rtswitch_context_t, wake_up_delay);
	rtswitch_task_t *task;

	task = &ctx->tasks[ctx->next_task];

	switch (task->base.flags & RTSWITCH_RT) {
	case RTSWITCH_NRT:
		ctx->utask = task;
		rtdm_nrtsig_pend(&ctx->wake_utask);
		break;

	case RTSWITCH_RT:
		rtdm_event_signal(&task->rt_synch);
	}
}

static int rtswitch_to_rt(rtswitch_context_t *ctx,
			  unsigned from_idx,
			  unsigned to_idx)
{
	rtswitch_task_t *from, *to;
	int rc;

	if (from_idx > ctx->tasks_count || to_idx > ctx->tasks_count)
		return -EINVAL;

	/* to == from is a special case which means
	   "return to the previous task". */
	if (to_idx == from_idx)
		to_idx = ctx->error.last_switch.from;

	from = &ctx->tasks[from_idx];
	to = &ctx->tasks[to_idx];

	from->base.flags |= RTSWITCH_RT;
	from->last_switch = ++ctx->switches_count;
	ctx->error.last_switch.from = from_idx;
	ctx->error.last_switch.to = to_idx;
	barrier();

	if (ctx->pause_us) {
		ctx->next_task = to_idx;
		barrier();
		rtdm_timer_start(&ctx->wake_up_delay,
				 ctx->pause_us * 1000, 0,
				 RTDM_TIMERMODE_RELATIVE);
		xnpod_lock_sched();
	} else
		switch (to->base.flags & RTSWITCH_RT) {
		case RTSWITCH_NRT:
			ctx->utask = to;
			barrier();
			rtdm_nrtsig_pend(&ctx->wake_utask);
			xnpod_lock_sched();
			break;

		case RTSWITCH_RT:
			xnpod_lock_sched();
			rtdm_event_signal(&to->rt_synch);
			break;

		default:
			return -EINVAL;
		}

	rc = rtdm_event_wait(&from->rt_synch);
	xnpod_unlock_sched();

	if (rc < 0)
		return rc;

	if (ctx->failed)
		return 1;

	return 0;
}

static int rtswitch_pend_nrt(rtswitch_context_t *ctx,
			     unsigned idx)
{
	rtswitch_task_t *task;

	if (idx > ctx->tasks_count)
		return -EINVAL;

	task = &ctx->tasks[idx];

	task->base.flags &= ~RTSWITCH_RT;

	if (down_interruptible(&task->nrt_synch))
		return -EINTR;

	if (ctx->failed)
		return 1;

	return 0;
}

static int rtswitch_to_nrt(rtswitch_context_t *ctx,
			   unsigned from_idx,
			   unsigned to_idx)
{
	rtswitch_task_t *from, *to;
	unsigned expected, fp_val;
	int fp_check;

	if (from_idx > ctx->tasks_count || to_idx > ctx->tasks_count)
		return -EINVAL;

	/* to == from is a special case which means
	   "return to the previous task". */
	if (to_idx == from_idx)
		to_idx = ctx->error.last_switch.from;

	from = &ctx->tasks[from_idx];
	to = &ctx->tasks[to_idx];

	fp_check = ctx->switches_count == from->last_switch + 1
		&& ctx->error.last_switch.from == to_idx
		&& ctx->error.last_switch.to == from_idx;

	from->base.flags &= ~RTSWITCH_RT;
	from->last_switch = ++ctx->switches_count;
	ctx->error.last_switch.from = from_idx;
	ctx->error.last_switch.to = to_idx;
	barrier();

	if (ctx->pause_us) {
		ctx->next_task = to_idx;
		barrier();
		rtdm_timer_start(&ctx->wake_up_delay,
				 ctx->pause_us * 1000, 0,
				 RTDM_TIMERMODE_RELATIVE);
	} else
		switch (to->base.flags & RTSWITCH_RT) {
		case RTSWITCH_NRT:
		switch_to_nrt:
			up(&to->nrt_synch);
			break;

		case RTSWITCH_RT:

			if (!fp_check || fp_linux_begin() < 0) {
				fp_check = 0;
				goto signal_nofp;
			}

			expected = from_idx + 500 +
				(ctx->switches_count % 4000000) * 1000;

			fp_regs_set(expected);
			rtdm_event_signal(&to->rt_synch);
			fp_val = fp_regs_check(expected);
			fp_linux_end();

			if(down_interruptible(&from->nrt_synch))
				return -EINTR;
			if (ctx->failed)
				return 1;
			if (fp_val != expected) {
				handle_ktask_error(ctx, fp_val);
				return 1;
			}

			from->base.flags &= ~RTSWITCH_RT;
			from->last_switch = ++ctx->switches_count;
			ctx->error.last_switch.from = from_idx;
			ctx->error.last_switch.to = to_idx;
			if ((to->base.flags & RTSWITCH_RT) == RTSWITCH_NRT)
				goto switch_to_nrt;
			expected = from_idx + 500 +
				(ctx->switches_count % 4000000) * 1000;
			barrier();

			fp_linux_begin();
			fp_regs_set(expected);
			rtdm_event_signal(&to->rt_synch);
			fp_val = fp_regs_check(expected);
			fp_linux_end();

			if (down_interruptible(&from->nrt_synch))
				return -EINTR;
			if (ctx->failed)
				return 1;
			if (fp_val != expected) {
				handle_ktask_error(ctx, fp_val);
				return 1;
			}

			from->base.flags &= ~RTSWITCH_RT;
			from->last_switch = ++ctx->switches_count;
			ctx->error.last_switch.from = from_idx;
			ctx->error.last_switch.to = to_idx;
			barrier();
			if ((to->base.flags & RTSWITCH_RT) == RTSWITCH_NRT)
				goto switch_to_nrt;

		signal_nofp:
			rtdm_event_signal(&to->rt_synch);
			break;

		default:
			return -EINVAL;
		}

	if (down_interruptible(&from->nrt_synch))
		return -EINTR;

	if (ctx->failed)
		return 1;

	return 0;
}

static int rtswitch_set_tasks_count(rtswitch_context_t *ctx, unsigned count)
{
	rtswitch_task_t *tasks;

	if (ctx->tasks_count == count)
		return 0;

	tasks = vmalloc(count * sizeof(*tasks));

	if (!tasks)
		return -ENOMEM;

	down(&ctx->lock);

	if (ctx->tasks)
		vfree(ctx->tasks);

	ctx->tasks = tasks;
	ctx->tasks_count = count;
	ctx->next_index = 0;

	up(&ctx->lock);

	return 0;
}

static int rtswitch_register_task(rtswitch_context_t *ctx,
				  struct rttst_swtest_task *arg)
{
	rtswitch_task_t *t;

	down(&ctx->lock);

	if (ctx->next_index == ctx->tasks_count) {
		up(&ctx->lock);
		return -EBUSY;
	}

	arg->index = ctx->next_index;
	t = &ctx->tasks[arg->index];
	ctx->next_index++;
	t->base = *arg;
	t->last_switch = 0;
	sema_init(&t->nrt_synch, 0);
	rtdm_event_init(&t->rt_synch, 0);

	up(&ctx->lock);

	return 0;
}

struct taskarg {
	rtswitch_context_t *ctx;
	rtswitch_task_t *task;
};

static void rtswitch_ktask(void *cookie)
{
	struct taskarg *arg = (struct taskarg *) cookie;
	rtswitch_context_t *ctx = arg->ctx;
	rtswitch_task_t *task = arg->task;
	unsigned to, i = 0;

	to = task->base.index;

	rtswitch_pend_rt(ctx, task->base.index);

	for(;;) {
		if (task->base.flags & RTTST_SWTEST_USE_FPU)
			fp_regs_set(task->base.index + i * 1000);

		switch(i % 3) {
		case 0:
			/* to == from means "return to last task" */
			rtswitch_to_rt(ctx, task->base.index, task->base.index);
			break;
		case 1:
			if (++to == task->base.index)
				++to;
			if (to > ctx->tasks_count - 1)
				to = 0;
			if (to == task->base.index)
				++to;

			/* Fall through. */
		case 2:
			rtswitch_to_rt(ctx, task->base.index, to);
		}

		if (task->base.flags & RTTST_SWTEST_USE_FPU) {
			unsigned fp_val, expected;

			expected = task->base.index + i * 1000;
			fp_val = fp_regs_check(expected);

			if (fp_val != expected) {
				if (task->base.flags & RTTST_SWTEST_FREEZE)
					xntrace_user_freeze(0, 0);
				handle_ktask_error(ctx, fp_val);
			}
		}

		if (++i == 4000000)
			i = 0;
	}
}

static int rtswitch_create_ktask(rtswitch_context_t *ctx,
				 struct rttst_swtest_task *ptask)
{
	union xnsched_policy_param param;
	struct xnthread_start_attr sattr;
	struct xnthread_init_attr iattr;
	rtswitch_task_t *task;
	xnflags_t init_flags;
	struct taskarg arg;
	char name[30];
	int err;

	/*
	 * Silently disable FP tests in kernel if FPU is not supported
	 * there. Typical case is math emulation support: we can use
	 * it from userland as a synthetic FPU, but there is no sane
	 * way to use it from kernel-based threads (Xenomai or Linux).
	 */
	if (!fp_kernel_supported())
		ptask->flags &= ~RTTST_SWTEST_USE_FPU;

	ptask->flags |= RTSWITCH_KERNEL;
	err = rtswitch_register_task(ctx, ptask);

	if (err)
		return err;

	snprintf(name, sizeof(name), "rtk%d/%u", ptask->index, ctx->cpu);

	task = &ctx->tasks[ptask->index];

	arg.ctx = ctx;
	arg.task = task;

	init_flags = (ptask->flags & RTTST_SWTEST_FPU) ? XNFPU : 0;

	/*
	 * Migrate the calling thread to the same CPU as the created
	 * task, in order to be sure that the created task is
	 * suspended when this function returns. This also allow us to
	 * use the stack to pass the parameters to the created
	 * task.
	 */
	set_cpus_allowed(current, cpumask_of_cpu(ctx->cpu));

	iattr.tbase = rtdm_tbase;
	iattr.name = name;
	iattr.flags = init_flags;
	iattr.ops = NULL;
	iattr.stacksize = 0;
	param.rt.prio = 1;

	err = xnpod_init_thread(&task->ktask,
				&iattr, &xnsched_class_rt, &param);
	if (!err) {
		sattr.mode = 0;
		sattr.imask = 0;
		sattr.affinity = xnarch_cpumask_of_cpu(ctx->cpu);
		sattr.entry = rtswitch_ktask;
		sattr.cookie = &arg;
		err = xnpod_start_thread(&task->ktask, &sattr);
	} else
		/*
		 * In order to avoid calling xnpod_delete_thread with
		 * invalid thread.
		 */
		task->base.flags = 0;
	/*
	 * Putting the argument on stack is safe, because the new
	 * thread will preempt the current thread immediately, and
	 * will suspend only once the arguments on stack are used.
	 */

	return err;
}

static void rtswitch_utask_waker(rtdm_nrtsig_t sig, void *arg)
{
	rtswitch_context_t *ctx = (rtswitch_context_t *)arg;
	up(&ctx->utask->nrt_synch);
}

static int rtswitch_open(struct rtdm_dev_context *context,
			 rtdm_user_info_t *user_info,
			 int oflags)
{
	rtswitch_context_t *ctx = (rtswitch_context_t *) context->dev_private;
	int err;

	ctx->tasks = NULL;
	ctx->tasks_count = ctx->next_index = ctx->cpu = ctx->switches_count = 0;
	sema_init(&ctx->lock, 1);
	ctx->failed = 0;
	ctx->error.last_switch.from = ctx->error.last_switch.to = -1;
	ctx->pause_us = 0;

	err = rtdm_nrtsig_init(&ctx->wake_utask, rtswitch_utask_waker, ctx);
	if (err)
		return err;

	rtdm_timer_init(&ctx->wake_up_delay, timed_wake_up, "switchtest timer");

	return 0;
}

static int rtswitch_close(struct rtdm_dev_context *context,
			  rtdm_user_info_t *user_info)
{
	rtswitch_context_t *ctx = (rtswitch_context_t *) context->dev_private;
	unsigned i;

	if (ctx->tasks) {
		set_cpus_allowed(current, cpumask_of_cpu(ctx->cpu));

		for (i = 0; i < ctx->next_index; i++) {
			rtswitch_task_t *task = &ctx->tasks[i];

			if (task->base.flags & RTSWITCH_KERNEL)
				xnpod_delete_thread(&task->ktask);
			rtdm_event_destroy(&task->rt_synch);
		}
		vfree(ctx->tasks);
	}
	rtdm_timer_destroy(&ctx->wake_up_delay);
	rtdm_nrtsig_destroy(&ctx->wake_utask);

	return 0;
}

static int rtswitch_ioctl_nrt(struct rtdm_dev_context *context,
			      rtdm_user_info_t *user_info,
			      unsigned int request,
			      void *arg)
{
	rtswitch_context_t *ctx = (rtswitch_context_t *) context->dev_private;
	struct rttst_swtest_task task;
	struct rttst_swtest_dir fromto;
	unsigned long count;
	int err;

	switch (request) {
	case RTTST_RTIOC_SWTEST_SET_TASKS_COUNT:
		return rtswitch_set_tasks_count(ctx,
						(unsigned long) arg);

	case RTTST_RTIOC_SWTEST_SET_CPU:
		if ((unsigned long) arg > xnarch_num_online_cpus() - 1)
			return -EINVAL;

		ctx->cpu = (unsigned long) arg;
		return 0;

	case RTTST_RTIOC_SWTEST_SET_PAUSE:
		ctx->pause_us = (unsigned long) arg;
		return 0;

	case RTTST_RTIOC_SWTEST_REGISTER_UTASK:
		if (!rtdm_rw_user_ok(user_info, arg, sizeof(task)))
			return -EFAULT;

		rtdm_copy_from_user(user_info, &task, arg, sizeof(task));

		err = rtswitch_register_task(ctx, &task);

		if (!err)
			rtdm_copy_to_user(user_info,
					  arg,
					  &task,
					  sizeof(task));

		return err;

	case RTTST_RTIOC_SWTEST_CREATE_KTASK:
		if (!rtdm_rw_user_ok(user_info, arg, sizeof(task)))
			return -EFAULT;

		rtdm_copy_from_user(user_info, &task, arg, sizeof(task));

		err = rtswitch_create_ktask(ctx, &task);

		if (!err)
			rtdm_copy_to_user(user_info,
					  arg,
					  &task,
					  sizeof(task));

		return err;

	case RTTST_RTIOC_SWTEST_PEND:
		if (!rtdm_read_user_ok(user_info, arg, sizeof(task)))
			return -EFAULT;

		rtdm_copy_from_user(user_info, &task, arg, sizeof(task));

		return rtswitch_pend_nrt(ctx, task.index);

	case RTTST_RTIOC_SWTEST_SWITCH_TO:
		if (!rtdm_read_user_ok(user_info, arg, sizeof(fromto)))
			return -EFAULT;

		rtdm_copy_from_user(user_info,
				    &fromto,
				    arg,
				    sizeof(fromto));

		return rtswitch_to_nrt(ctx, fromto.from, fromto.to);

	case RTTST_RTIOC_SWTEST_GET_SWITCHES_COUNT:
		if (!rtdm_rw_user_ok(user_info, arg, sizeof(count)))
			return -EFAULT;

		count = ctx->switches_count;

		rtdm_copy_to_user(user_info, arg, &count, sizeof(count));

		return 0;

	case RTTST_RTIOC_SWTEST_GET_LAST_ERROR:
		if (!rtdm_rw_user_ok(user_info, arg, sizeof(ctx->error)))
			return -EFAULT;

		rtdm_copy_to_user(user_info,
				  arg,
				  &ctx->error,
				  sizeof(ctx->error));

		return 0;

	default:
		return -ENOTTY;
	}
}

static int rtswitch_ioctl_rt(struct rtdm_dev_context *context,
			     rtdm_user_info_t *user_info,
			     unsigned int request,
			     void *arg)
{
	rtswitch_context_t *ctx = (rtswitch_context_t *) context->dev_private;
	struct rttst_swtest_task task;
	struct rttst_swtest_dir fromto;

	switch (request) {
	case RTTST_RTIOC_SWTEST_REGISTER_UTASK:
	case RTTST_RTIOC_SWTEST_CREATE_KTASK:
	case RTTST_RTIOC_SWTEST_GET_SWITCHES_COUNT:
		return -ENOSYS;

	case RTTST_RTIOC_SWTEST_PEND:
		if (!rtdm_read_user_ok(user_info, arg, sizeof(task)))
			return -EFAULT;

		rtdm_copy_from_user(user_info, &task, arg, sizeof(task));

		return rtswitch_pend_rt(ctx, task.index);

	case RTTST_RTIOC_SWTEST_SWITCH_TO:
		if (!rtdm_read_user_ok(user_info, arg, sizeof(fromto)))
			return -EFAULT;

		rtdm_copy_from_user(user_info,
				    &fromto,
				    arg,
				    sizeof(fromto));

		return rtswitch_to_rt(ctx, fromto.from, fromto.to);

	case RTTST_RTIOC_SWTEST_GET_LAST_ERROR:
		if (!rtdm_rw_user_ok(user_info, arg, sizeof(ctx->error)))
			return -EFAULT;

		rtdm_copy_to_user(user_info,
				  arg,
				  &ctx->error,
				  sizeof(ctx->error));

		return 0;

	default:
		return -ENOTTY;
	}
}

static struct rtdm_device device = {
	struct_version: RTDM_DEVICE_STRUCT_VER,

	device_flags: RTDM_NAMED_DEVICE,
	context_size: sizeof(rtswitch_context_t),
	device_name:  "",

	open_rt: NULL,
	open_nrt: rtswitch_open,

	ops: {
		close_rt: NULL,
		close_nrt: rtswitch_close,

		ioctl_rt: rtswitch_ioctl_rt,
		ioctl_nrt: rtswitch_ioctl_nrt,

		read_rt: NULL,
		read_nrt: NULL,

		write_rt: NULL,
		write_nrt: NULL,

		recvmsg_rt: NULL,
		recvmsg_nrt: NULL,

		sendmsg_rt: NULL,
		sendmsg_nrt: NULL,
	},

	device_class: RTDM_CLASS_TESTING,
	device_sub_class: RTDM_SUBCLASS_SWITCHTEST,
	profile_version: RTTST_PROFILE_VER,
	driver_name: "xeno_switchtest",
	driver_version: RTDM_DRIVER_VER(0, 1, 1),
	peripheral_name: "Context Switch Test",
	provider_name: "Gilles Chanteperdrix",
	proc_name: device.device_name,
};

int __init __switchtest_init(void)
{
	int err;

	fp_features_init();

	do {
		snprintf(device.device_name, RTDM_MAX_DEVNAME_LEN,
			 "rttest-switchtest%d",
			 start_index);
		err = rtdm_dev_register(&device);

		start_index++;
	} while (err == -EEXIST);

	return err;
}

void __switchtest_exit(void)
{
	rtdm_dev_unregister(&device, 1000);
}

module_init(__switchtest_init);
module_exit(__switchtest_exit);

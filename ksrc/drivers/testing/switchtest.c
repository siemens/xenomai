#include <nucleus/synch.h>
#include <nucleus/thread.h>
#include <rtdm/rttesting.h>
#include <rtdm/rtdm_driver.h>
#include <asm/xenomai/fptest.h>
#include <asm/semaphore.h>

#define RTSWITCH_RT      0x4
#define RTSWITCH_NRT     0
#define RTSWITCH_KERNEL  0x8

typedef struct {
    struct rtswitch_task base;
    xnsynch_t rt_synch;
    struct semaphore nrt_synch;
    xnthread_t ktask;          /* For kernel-space real-time tasks. */
} rtswitch_task_t;

typedef struct rtswitch_context {
    rtswitch_task_t *tasks;
    unsigned tasks_count;
    unsigned next_index;
    struct semaphore lock;
    unsigned cpu;
    unsigned switches_count;
} rtswitch_context_t;

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gilles.Chanteperdrix@laposte.net");

static rtswitch_task_t *rtswitch_utask[NR_CPUS];
static rtdm_nrtsig_t rtswitch_wake_utask;

static int rtswitch_pend_rt(rtswitch_context_t *ctx,
                            unsigned idx)
{
    rtswitch_task_t *task;

    if (idx > ctx->tasks_count)
        return -EINVAL;

    task = &ctx->tasks[idx];
    task->base.flags |= RTSWITCH_RT;

    xnsynch_sleep_on(&task->rt_synch, XN_INFINITE);

    if (xnthread_test_flags(xnpod_current_thread(), XNBREAK))
        return -EINTR;

    if (xnthread_test_flags(xnpod_current_thread(), XNRMID))
        return -EIDRM;

    return 0;
}

static int rtswitch_to_rt(rtswitch_context_t *ctx,
                           unsigned from_idx,
                           unsigned to_idx)
{
    rtswitch_task_t *from, *to;
    spl_t s;

    if (from_idx > ctx->tasks_count || to_idx > ctx->tasks_count)
        return -EINVAL;

    from = &ctx->tasks[from_idx];
    to = &ctx->tasks[to_idx];

    from->base.flags |= RTSWITCH_RT;
    ++ctx->switches_count;

    switch (to->base.flags & RTSWITCH_RT) {
    case RTSWITCH_NRT:
        rtswitch_utask[ctx->cpu] = to;
        rtdm_nrtsig_pend(&rtswitch_wake_utask);
        xnlock_get_irqsave(&nklock, s);
        break;

    case RTSWITCH_RT:
        xnlock_get_irqsave(&nklock, s);

        xnsynch_wakeup_one_sleeper(&to->rt_synch);
        break;

    default:
        return -EINVAL;
    }

    xnsynch_sleep_on(&from->rt_synch, XN_INFINITE);

    xnlock_put_irqrestore(&nklock, s);

    if (xnthread_test_flags(xnpod_current_thread(), XNBREAK))
        return -EINTR;

    if (xnthread_test_flags(xnpod_current_thread(), XNRMID))
        return -EIDRM;

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

    return 0;
}

static int rtswitch_to_nrt(rtswitch_context_t *ctx,
                            unsigned from_idx,
                            unsigned to_idx)
{
    rtswitch_task_t *from, *to;

    if (from_idx > ctx->tasks_count || to_idx > ctx->tasks_count)
        return -EINVAL;

    from = &ctx->tasks[from_idx];
    to = &ctx->tasks[to_idx];

    from->base.flags &= ~RTSWITCH_RT;
    ++ctx->switches_count;

    switch (to->base.flags & RTSWITCH_RT) {
    case RTSWITCH_NRT:
        up(&to->nrt_synch);
        break;

    case RTSWITCH_RT:
        xnsynch_wakeup_one_sleeper(&to->rt_synch);
        xnpod_schedule();
        break;

    default:
        return -EINVAL;
    }

    if (down_interruptible(&from->nrt_synch))
        return -EINTR;

    return 0;
}

static int rtswitch_set_tasks_count(rtswitch_context_t *ctx, unsigned count)
{
    rtswitch_task_t *tasks;

    if (ctx->tasks_count == count)
        return 0;

    tasks = kmalloc(count * sizeof(*tasks), GFP_KERNEL);

    if (!tasks)
        return -ENOMEM;
    
    down(&ctx->lock);
    
    if (ctx->tasks)
        kfree(ctx->tasks);

    ctx->tasks = tasks;
    ctx->tasks_count = count;
    ctx->next_index = 0;

    up(&ctx->lock);

    return 0;
}

static int rtswitch_register_task(rtswitch_context_t *ctx,
                                  struct rtswitch_task *arg)
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
    sema_init(&t->nrt_synch, 0);
    xnsynch_init(&t->rt_synch, XNSYNCH_FIFO);

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
        if (++to == task->base.index)
            ++to;
        if (to > ctx->tasks_count - 1)
            to = 0;
        if (to == task->base.index)
            ++to;

        if (task->base.flags & RTSWITCH_USE_FPU)
            fp_regs_set(task->base.index + i * 1000);
        rtswitch_to_rt(ctx, task->base.index, to);
        if (task->base.flags & RTSWITCH_USE_FPU)
            if (fp_regs_check(task->base.index + i * 1000))
                xnpod_suspend_self();

        if (++i == 4000000)
            i = 0;
    }
}

static int rtswitch_create_ktask(rtswitch_context_t *ctx,
                                 struct rtswitch_task *ptask)
{
    rtswitch_task_t *task;
    xnflags_t init_flags;
    struct taskarg arg;
    char name[30];
    int err;

    ptask->flags |= RTSWITCH_KERNEL;
    err = rtswitch_register_task(ctx, ptask);

    if (err)
        return err;

    snprintf(name, sizeof(name), "rtk%d/%u", ptask->index, ctx->cpu);

    task = &ctx->tasks[ptask->index];

    arg.ctx = ctx;
    arg.task = task;

    init_flags = (ptask->flags & RTSWITCH_FPU) ? XNFPU : 0;

    /* Migrate the calling thread to the same CPU as the created task, in order
       to be sure that the created task is suspended when this function
       returns. This also allow us to use the stack to pass the parameters to
       the created task. */
    set_cpus_allowed(current, cpumask_of_cpu(ctx->cpu));
    
    err = xnpod_init_thread(&task->ktask, name, 1, init_flags, 0);

    if (!err)
        err = xnpod_start_thread(&task->ktask,
                                 0,
                                 0,
                                 xnarch_cpumask_of_cpu(ctx->cpu),
                                 rtswitch_ktask,
                                 &arg);

    /* Putting the argument on stack is safe, because the new thread will
       preempt the current thread immediately, and will suspend only once the
       arguments on stack are used. */

    return err;
}

static int rtswitch_open(struct rtdm_dev_context *context,
                         rtdm_user_info_t *user_info,
                         int oflags)
{
    rtswitch_context_t *ctx = (rtswitch_context_t *) context->dev_private;

    ctx->tasks = NULL;
    ctx->tasks_count = ctx->next_index = ctx->cpu = ctx->switches_count = 0;
    init_MUTEX(&ctx->lock);

    return 0;
}

static int rtswitch_close(struct rtdm_dev_context *context,
                          rtdm_user_info_t *user_info)
{
    rtswitch_context_t *ctx = (rtswitch_context_t *) context->dev_private;
    unsigned i;

    if (ctx->tasks) {
        for (i = 0; i < ctx->tasks_count; i++) {
            rtswitch_task_t *task = &ctx->tasks[i];

            if (task->base.flags & RTSWITCH_KERNEL)
                xnpod_delete_thread(&task->ktask);
            xnsynch_destroy(&task->rt_synch);
        }
        xnpod_schedule();
        kfree(ctx->tasks);
    }

    return 0;
}

static int rtswitch_ioctl_nrt(struct rtdm_dev_context *context,
                              rtdm_user_info_t *user_info,
                              int request,
                              void *arg)
{
    rtswitch_context_t *ctx = (rtswitch_context_t *) context->dev_private;
    struct rtswitch_task task;
    struct rtswitch fromto;
    unsigned long count;
    int err;

    switch (request)
        {
        case RTSWITCH_RTIOC_TASKS_COUNT:
            return rtswitch_set_tasks_count(ctx, (unsigned) arg);

        case RTSWITCH_RTIOC_SET_CPU:
            if ((unsigned) arg > xnarch_num_online_cpus())
                return -EINVAL;

            ctx->cpu = (unsigned) arg;
            return 0;

        case RTSWITCH_RTIOC_REGISTER_UTASK:
            if (!rtdm_rw_user_ok(user_info, arg, sizeof(task)))
                return -EFAULT;

            rtdm_copy_from_user(user_info, &task, arg, sizeof(task));

            err = rtswitch_register_task(ctx, &task);

            if (!err)
                rtdm_copy_to_user(user_info, arg, &task, sizeof(task));

            return err;

        case RTSWITCH_RTIOC_CREATE_KTASK:
            if (!rtdm_rw_user_ok(user_info, arg, sizeof(task)))
                return -EFAULT;

            rtdm_copy_from_user(user_info, &task, arg, sizeof(task));

            err = rtswitch_create_ktask(ctx, &task);

            if (!err)
                rtdm_copy_to_user(user_info, arg, &task, sizeof(task));

            return err;

        case RTSWITCH_RTIOC_PEND:
            if (!rtdm_read_user_ok(user_info, arg, sizeof(task)))
                return -EFAULT;

            rtdm_copy_from_user(user_info, &task, arg, sizeof(task));
            
            return rtswitch_pend_nrt(ctx, task.index);

        case RTSWITCH_RTIOC_SWITCH_TO:
            if (!rtdm_read_user_ok(user_info, arg, sizeof(fromto)))
                return -EFAULT;

            rtdm_copy_from_user(user_info, &fromto, arg, sizeof(fromto));

            rtswitch_to_nrt(ctx, fromto.from, fromto.to);

            return 0;

        case RTSWITCH_RTIOC_GET_SWITCHES_COUNT:
            if (!rtdm_rw_user_ok(user_info, arg, sizeof(count)))
                return -EFAULT;

            count = ctx->switches_count;

            rtdm_copy_to_user(user_info, arg, &count, sizeof(count));

            return 0;
            
        default:
            return -ENOTTY;
        }
}

static int rtswitch_ioctl_rt(struct rtdm_dev_context *context,
                             rtdm_user_info_t *user_info,
                             int request,
                             void *arg)
{
    rtswitch_context_t *ctx = (rtswitch_context_t *) context->dev_private;
    struct rtswitch_task task;
    struct rtswitch fromto;

    switch (request) 
        {
        case RTSWITCH_RTIOC_REGISTER_UTASK:
        case RTSWITCH_RTIOC_CREATE_KTASK:
        case RTSWITCH_RTIOC_GET_SWITCHES_COUNT:
            return -ENOSYS;

         case RTSWITCH_RTIOC_PEND:
            if (!rtdm_read_user_ok(user_info, arg, sizeof(task)))
                return -EFAULT;

            rtdm_copy_from_user(user_info, &task, arg, sizeof(task));
            
            return rtswitch_pend_rt(ctx, task.index);

        case RTSWITCH_RTIOC_SWITCH_TO:
            if (!rtdm_read_user_ok(user_info, arg, sizeof(fromto)))
                return -EFAULT;

            rtdm_copy_from_user(user_info, &fromto, arg, sizeof(fromto));

            rtswitch_to_rt(ctx, fromto.from, fromto.to);

            return 0;

       default:
            return -ENOTTY;
        }
}

static struct rtdm_device device = {
    struct_version: RTDM_DEVICE_STRUCT_VER,

    device_flags: RTDM_NAMED_DEVICE,
    context_size: sizeof(rtswitch_context_t),
    device_name:  "rtswitch0",

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
    device_sub_class: RTDM_SUBCLASS_SWITCH,
    driver_name: "xeno_switchbench",
    driver_version: RTDM_DRIVER_VER(0, 1, 0),
    peripheral_name: "Context switch benchmark",
    provider_name: "Gilles Chanteperdrix",
    proc_name: device.device_name,
};

void rtswitch_utask_waker(rtdm_nrtsig_t sig)
{
    up(&rtswitch_utask[xnarch_current_cpu()]->nrt_synch);
}

int __init __switchbench_init(void)
{
    int err;

    err = rtdm_nrtsig_init(&rtswitch_wake_utask, rtswitch_utask_waker);

    if (err)
        return err;
    
    return rtdm_dev_register(&device);
}

void __switchbench_exit(void)
{
    if(rtdm_dev_unregister(&device, 0))
        printk("Warning: could not unregister driver %s\n", device.device_name);
    rtdm_nrtsig_destroy(&rtswitch_wake_utask);
}

module_init(__switchbench_init);
module_exit(__switchbench_exit);

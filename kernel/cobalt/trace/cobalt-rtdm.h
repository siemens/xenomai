#undef TRACE_SYSTEM
#define TRACE_SYSTEM cobalt-rtdm

#if !defined(_TRACE_COBALT_RTDM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_COBALT_RTDM_H

#include <linux/tracepoint.h>

struct rtdm_event;
struct rtdm_sem;
struct rtdm_mutex;
struct xnthread;
struct rtdm_device;
struct rtdm_dev_context;

DECLARE_EVENT_CLASS(fd_event,
	TP_PROTO(struct rtdm_dev_context *context),
	TP_ARGS(context),

	TP_STRUCT__entry(
		__field(struct rtdm_device *, device)
		__field(int, fd)
	),

	TP_fast_assign(
		__entry->device = context->device;
		__entry->fd = context->fd;
	),

	TP_printk("device=%p fd=%d",
		  __entry->device, __entry->fd)
);

DECLARE_EVENT_CLASS(fd_request,
	TP_PROTO(struct task_struct *task,
		 struct rtdm_dev_context *context, unsigned long arg),
	TP_ARGS(task, context, arg),

	TP_STRUCT__entry(
		__field(struct task_struct *, task)
		__array(char, comm, TASK_COMM_LEN)
		__field(pid_t, pid)
		__field(struct rtdm_device *, device)
		__field(int, fd)
		__field(unsigned long, arg)
	),

	TP_fast_assign(
		__entry->task = task;
		memcpy(__entry->comm, task->comm, TASK_COMM_LEN);
		__entry->pid = task->pid;
		__entry->device = context->device;
		__entry->fd = context->fd;
		__entry->arg = arg;
	),

	TP_printk("device=%p fd=%d arg=%#lx pid=%d comm=%s",
		  __entry->device, __entry->fd, __entry->arg,
		  __entry->pid, __entry->comm)
);

DECLARE_EVENT_CLASS(fd_request_status,
	TP_PROTO(struct task_struct *task,
		 struct rtdm_dev_context *context, int status),
	TP_ARGS(task, context, status),

	TP_STRUCT__entry(
		__field(struct task_struct *, task)
		__array(char, comm, TASK_COMM_LEN)
		__field(pid_t, pid)
		__field(struct rtdm_device *, device)
		__field(int, fd)
	),

	TP_fast_assign(
		__entry->task = task;
		memcpy(__entry->comm, task->comm, TASK_COMM_LEN);
		__entry->pid = task->pid;
		__entry->device	= context->device;
		__entry->fd = context->fd;
	),

	TP_printk("device=%p fd=%d pid=%d comm=%s",
		  __entry->device, __entry->fd, __entry->pid, __entry->comm)
);

DECLARE_EVENT_CLASS(task_op,
	TP_PROTO(struct xnthread *task),
	TP_ARGS(task),

	TP_STRUCT__entry(
		__field(struct xnthread *, task)
		__string(task_name, xnthread_name(task))
	),

	TP_fast_assign(
		__entry->task = task;
		__assign_str(task_name, xnthread_name(task));
	),

	TP_printk("task %p(%s)", __entry->task, __get_str(task_name))
);

DECLARE_EVENT_CLASS(event_op,
	TP_PROTO(struct rtdm_event *ev),
	TP_ARGS(ev),

	TP_STRUCT__entry(
		__field(struct rtdm_event *, ev)
	),

	TP_fast_assign(
		__entry->ev = ev;
	),

	TP_printk("event=%p", __entry->ev)
);

DECLARE_EVENT_CLASS(sem_op,
	TP_PROTO(struct rtdm_sem *sem),
	TP_ARGS(sem),

	TP_STRUCT__entry(
		__field(struct rtdm_sem *, sem)
	),

	TP_fast_assign(
		__entry->sem = sem;
	),

	TP_printk("sem=%p", __entry->sem)
);

DECLARE_EVENT_CLASS(mutex_op,
	TP_PROTO(struct rtdm_mutex *mutex),
	TP_ARGS(mutex),

	TP_STRUCT__entry(
		__field(struct rtdm_mutex *, mutex)
	),

	TP_fast_assign(
		__entry->mutex = mutex;
	),

	TP_printk("mutex=%p", __entry->mutex)
);

TRACE_EVENT(cobalt_device_register,
	TP_PROTO(struct rtdm_device *device),
	TP_ARGS(device),

	TP_STRUCT__entry(
		__field(struct rtdm_device *, device)
		__string(device_name, device->device_name)
		__field(int, flags)
		__field(int, device_class)
		__field(int, device_subclass)
		__field(int, profile_version)
		__field(int, driver_version)
	),

	TP_fast_assign(
		__entry->device	= device;
		__assign_str(device_name, device->device_name);
		__entry->flags = device->device_flags;
		__entry->device_class = device->device_class;
		__entry->device_subclass = device->device_sub_class;
		__entry->profile_version = device->profile_version;
		__entry->driver_version = device->driver_version;
	),

	TP_printk("%s device %s=%p version=%d flags=0x%x, class=%d.%d profile=%d",
		  (__entry->flags & RTDM_DEVICE_TYPE_MASK)
		  == RTDM_NAMED_DEVICE ? "named" : "protocol",
		  __get_str(device_name), __entry->device, __entry->driver_version,
		  __entry->flags, __entry->device_class, __entry->device_subclass,
		  __entry->profile_version)
);

TRACE_EVENT(cobalt_device_unregister,
	TP_PROTO(struct rtdm_device *device, unsigned int poll_delay),
	TP_ARGS(device, poll_delay),

	TP_STRUCT__entry(
		__field(struct rtdm_device *, device)
		__string(device_name, device->device_name)
		__field(unsigned int, poll_delay)
	),

	TP_fast_assign(
		__entry->device	= device;
		__assign_str(device_name, device->device_name);
		__entry->poll_delay = poll_delay;
	),

	TP_printk("device %s=%p poll_delay=%u",
		  __get_str(device_name), __entry->device, __entry->poll_delay)
);

DEFINE_EVENT(fd_event, cobalt_fd_created,
	TP_PROTO(struct rtdm_dev_context *context),
	TP_ARGS(context)
);

DEFINE_EVENT(fd_event, cobalt_fd_closed,
	TP_PROTO(struct rtdm_dev_context *context),
	TP_ARGS(context)
);

DEFINE_EVENT(fd_request, cobalt_fd_open,
	TP_PROTO(struct task_struct *task,
		 struct rtdm_dev_context *context,
		 unsigned long oflags),
	TP_ARGS(task, context, oflags)
);

DEFINE_EVENT(fd_request, cobalt_fd_close,
	TP_PROTO(struct task_struct *task,
		 struct rtdm_dev_context *context,
		 unsigned long lock_count),
	TP_ARGS(task, context, lock_count)
);

DEFINE_EVENT(fd_request, cobalt_fd_socket,
	TP_PROTO(struct task_struct *task,
		 struct rtdm_dev_context *context,
		 unsigned long protocol_family),
	TP_ARGS(task, context, protocol_family)
);

DEFINE_EVENT(fd_request, cobalt_fd_read,
	TP_PROTO(struct task_struct *task,
		 struct rtdm_dev_context *context,
		 unsigned long len),
	TP_ARGS(task, context, len)
);

DEFINE_EVENT(fd_request, cobalt_fd_write,
	TP_PROTO(struct task_struct *task,
		 struct rtdm_dev_context *context,
		 unsigned long len),
	TP_ARGS(task, context, len)
);

DEFINE_EVENT(fd_request, cobalt_fd_ioctl,
	TP_PROTO(struct task_struct *task,
		 struct rtdm_dev_context *context,
		 unsigned long request),
	TP_ARGS(task, context, request)
);

DEFINE_EVENT(fd_request, cobalt_fd_sendmsg,
	TP_PROTO(struct task_struct *task,
		 struct rtdm_dev_context *context,
		 unsigned long flags),
	TP_ARGS(task, context, flags)
);

DEFINE_EVENT(fd_request, cobalt_fd_recvmsg,
	TP_PROTO(struct task_struct *task,
		 struct rtdm_dev_context *context,
		 unsigned long flags),
	TP_ARGS(task, context, flags)
);

DEFINE_EVENT(fd_request_status, cobalt_fd_ioctl_status,
	TP_PROTO(struct task_struct *task,
		 struct rtdm_dev_context *context,
		 int status),
	TP_ARGS(task, context, status)
);

DEFINE_EVENT(fd_request_status, cobalt_fd_read_status,
	TP_PROTO(struct task_struct *task,
		 struct rtdm_dev_context *context,
		 int status),
	TP_ARGS(task, context, status)
);

DEFINE_EVENT(fd_request_status, cobalt_fd_write_status,
	TP_PROTO(struct task_struct *task,
		 struct rtdm_dev_context *context,
		 int status),
	TP_ARGS(task, context, status)
);

DEFINE_EVENT(fd_request_status, cobalt_fd_recvmsg_status,
	TP_PROTO(struct task_struct *task,
		 struct rtdm_dev_context *context,
		 int status),
	TP_ARGS(task, context, status)
);

DEFINE_EVENT(fd_request_status, cobalt_fd_sendmsg_status,
	TP_PROTO(struct task_struct *task,
		 struct rtdm_dev_context *context,
		 int status),
	TP_ARGS(task, context, status)
);

DEFINE_EVENT(task_op, cobalt_driver_task_join,
	TP_PROTO(struct xnthread *task),
	TP_ARGS(task)
);

TRACE_EVENT(cobalt_driver_event_init,
	TP_PROTO(struct rtdm_event *ev, unsigned long pending),
	TP_ARGS(ev, pending),

	TP_STRUCT__entry(
		__field(struct rtdm_event *, ev)
		__field(unsigned long,	pending)
	),

	TP_fast_assign(
		__entry->ev = ev;
		__entry->pending = pending;
	),

	TP_printk("event=%p pending=%#lx",
		  __entry->ev, __entry->pending)
);

TRACE_EVENT(cobalt_driver_event_wait,
	TP_PROTO(struct rtdm_event *ev, struct xnthread *task),
	TP_ARGS(ev, task),

	TP_STRUCT__entry(
		__field(struct xnthread *, task)
		__string(task_name, xnthread_name(task))
		__field(struct rtdm_event *, ev)
	),

	TP_fast_assign(
		__entry->task = task;
		__assign_str(task_name, xnthread_name(task));
		__entry->ev = ev;
	),

	TP_printk("event=%p task=%p(%s)",
		  __entry->ev, __entry->task, __get_str(task_name))
);

DEFINE_EVENT(event_op, cobalt_driver_event_signal,
	TP_PROTO(struct rtdm_event *ev),
	TP_ARGS(ev)
);

DEFINE_EVENT(event_op, cobalt_driver_event_clear,
	TP_PROTO(struct rtdm_event *ev),
	TP_ARGS(ev)
);

DEFINE_EVENT(event_op, cobalt_driver_event_pulse,
	TP_PROTO(struct rtdm_event *ev),
	TP_ARGS(ev)
);

DEFINE_EVENT(event_op, cobalt_driver_event_destroy,
	TP_PROTO(struct rtdm_event *ev),
	TP_ARGS(ev)
);

TRACE_EVENT(cobalt_driver_sem_init,
	TP_PROTO(struct rtdm_sem *sem, unsigned long value),
	TP_ARGS(sem, value),

	TP_STRUCT__entry(
		__field(struct rtdm_sem *, sem)
		__field(unsigned long, value)
	),

	TP_fast_assign(
		__entry->sem = sem;
		__entry->value = value;
	),

	TP_printk("sem=%p value=%lu",
		  __entry->sem, __entry->value)
);

TRACE_EVENT(cobalt_driver_sem_wait,
	TP_PROTO(struct rtdm_sem *sem, struct xnthread *task),
	TP_ARGS(sem, task),

	TP_STRUCT__entry(
		__field(struct xnthread *, task)
		__string(task_name, xnthread_name(task))
		__field(struct rtdm_sem *, sem)
	),

	TP_fast_assign(
		__entry->task = task;
		__assign_str(task_name, xnthread_name(task));
		__entry->sem = sem;
	),

	TP_printk("sem=%p task=%p(%s)",
		  __entry->sem, __entry->task, __get_str(task_name))
);

DEFINE_EVENT(sem_op, cobalt_driver_sem_up,
	TP_PROTO(struct rtdm_sem *sem),
	TP_ARGS(sem)
);

DEFINE_EVENT(sem_op, cobalt_driver_sem_destroy,
	TP_PROTO(struct rtdm_sem *sem),
	TP_ARGS(sem)
);

DEFINE_EVENT(mutex_op, cobalt_driver_mutex_init,
	TP_PROTO(struct rtdm_mutex *mutex),
	TP_ARGS(mutex)
);

DEFINE_EVENT(mutex_op, cobalt_driver_mutex_release,
	TP_PROTO(struct rtdm_mutex *mutex),
	TP_ARGS(mutex)
);

DEFINE_EVENT(mutex_op, cobalt_driver_mutex_destroy,
	TP_PROTO(struct rtdm_mutex *mutex),
	TP_ARGS(mutex)
);

TRACE_EVENT(cobalt_driver_mutex_wait,
	TP_PROTO(struct rtdm_mutex *mutex, struct xnthread *task),
	TP_ARGS(mutex, task),

	TP_STRUCT__entry(
		__field(struct xnthread *, task)
		__string(task_name, xnthread_name(task))
		__field(struct rtdm_mutex *, mutex)
	),

	TP_fast_assign(
		__entry->task = task;
		__assign_str(task_name, xnthread_name(task));
		__entry->mutex = mutex;
	),

	TP_printk("mutex=%p task=%p(%s)",
		  __entry->mutex, __entry->task, __get_str(task_name))
);

#endif /* _TRACE_COBALT_RTDM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>

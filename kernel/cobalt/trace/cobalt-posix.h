#undef TRACE_SYSTEM
#define TRACE_SYSTEM cobalt-posix

#if !defined(_TRACE_COBALT_POSIX_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_COBALT_POSIX_H

#include <linux/tracepoint.h>

#define __timespec_fields(__name)				\
	__field(__kernel_time_t, tv_sec_##__name)		\
	__field(long, tv_nsec_##__name)

#define __assign_timespec(__to, __from)				\
	do {							\
		__entry->tv_sec_##__to = (__from)->tv_sec;	\
		__entry->tv_nsec_##__to = (__from)->tv_nsec;	\
	} while (0)

#define __timespec_args(__name)					\
	__entry->tv_sec_##__name, __entry->tv_nsec_##__name

#define cobalt_print_sched_policy(__policy)			\
	__print_symbolic(__policy,				\
			 {SCHED_NORMAL, "normal"},		\
			 {SCHED_FIFO, "fifo"},			\
			 {SCHED_RR, "rr"},			\
			 {SCHED_TP, "tp"},			\
			 {SCHED_QUOTA, "quota"},		\
			 {SCHED_SPORADIC, "sporadic"},		\
			 {SCHED_COBALT, "cobalt"},		\
			 {SCHED_WEAK, "weak"})

#define cobalt_print_sched_params(__policy, __p_ex)			\
({									\
  	const char *__ret = p->buffer + p->len;				\
	switch (__policy) {						\
	case SCHED_QUOTA:						\
		trace_seq_printf(p, "priority=%d, group=%d",		\
				 (__p_ex)->sched_priority,		\
				 (__p_ex)->sched_quota_group);		\
		break;							\
	case SCHED_TP:							\
		trace_seq_printf(p, "priority=%d, partition=%d",	\
				 (__p_ex)->sched_priority,		\
				 (__p_ex)->sched_tp_partition);		\
		break;							\
	case SCHED_NORMAL:						\
		break;							\
	case SCHED_SPORADIC:						\
		trace_seq_printf(p, "priority=%d, low_priority=%d, "	\
				 "budget=(%ld.%09ld), period=(%ld.%09ld), "\
				 "maxrepl=%d",				\
				 (__p_ex)->sched_priority,		\
				 (__p_ex)->sched_ss_low_priority,	\
				 (__p_ex)->sched_ss_init_budget.tv_sec,	\
				 (__p_ex)->sched_ss_init_budget.tv_nsec, \
				 (__p_ex)->sched_ss_repl_period.tv_sec,	\
				 (__p_ex)->sched_ss_repl_period.tv_nsec, \
				 (__p_ex)->sched_ss_max_repl);		\
		break;							\
	case SCHED_RR:							\
	case SCHED_FIFO:						\
	case SCHED_COBALT:						\
	case SCHED_WEAK:						\
	default:							\
		trace_seq_printf(p, "priority=%d",			\
				 (__p_ex)->sched_priority);		\
		break;							\
	}								\
	__ret;								\
})

DECLARE_EVENT_CLASS(cobalt_posix_timespec,
	TP_PROTO(struct timespec *ts),
	TP_ARGS(ts),

	TP_STRUCT__entry(
		__timespec_fields(ts)
	),

	TP_fast_assign(
		__assign_timespec(ts, ts);
	),

	TP_printk("time=(%ld.%09ld)", __timespec_args(ts))
);

DECLARE_EVENT_CLASS(cobalt_posix_schedparam,
	TP_PROTO(unsigned long pth, int policy,
		 struct sched_param_ex *param_ex),
	TP_ARGS(pth, policy, param_ex),

	TP_STRUCT__entry(
		__field(unsigned long, pth)
		__field(int, policy)
		__dynamic_array(char, param_ex, sizeof(struct sched_param_ex))
	),

	TP_fast_assign(
		__entry->pth = pth;
		__entry->policy = policy;
		memcpy(__get_dynamic_array(param_ex), param_ex, sizeof(*param_ex));
	),

	TP_printk("pth=%p policy=%d(%s) param={ %s }",
		  (void *)__entry->pth, __entry->policy,
		  cobalt_print_sched_policy(__entry->policy),
		  cobalt_print_sched_params(__entry->policy,
					    (struct sched_param_ex *)
					    __get_dynamic_array(param_ex))
	)
);

DECLARE_EVENT_CLASS(cobalt_void,
	TP_PROTO(int dummy),
	TP_ARGS(dummy),
	TP_STRUCT__entry(
		__array(char, dummy, 0)
	),
	TP_fast_assign(
		(void)dummy;
	),
	TP_printk("%s", "")
);

DEFINE_EVENT(cobalt_posix_schedparam, cobalt_pthread_create,
	TP_PROTO(unsigned long pth, int policy,
		 struct sched_param_ex *param_ex),
	TP_ARGS(pth, policy, param_ex)
);

DEFINE_EVENT(cobalt_posix_schedparam, cobalt_pthread_setschedparam,
	TP_PROTO(unsigned long pth, int policy,
		 struct sched_param_ex *param_ex),
	TP_ARGS(pth, policy, param_ex)
);

DEFINE_EVENT(cobalt_posix_schedparam, cobalt_pthread_getschedparam,
	TP_PROTO(unsigned long pth, int policy,
		 struct sched_param_ex *param_ex),
	TP_ARGS(pth, policy, param_ex)
);

TRACE_EVENT(cobalt_pthread_make_periodic,
	TP_PROTO(unsigned long pth, clockid_t clk_id,
		 struct timespec *start, struct timespec *period),
	TP_ARGS(pth, clk_id, start, period),

	TP_STRUCT__entry(
		__field(unsigned long, pth)
		__field(clockid_t, clk_id)
		__timespec_fields(start)
		__timespec_fields(period)
	),

	TP_fast_assign(
		__entry->pth = pth;
		__entry->clk_id = clk_id;
		__assign_timespec(start, start);
		__assign_timespec(period, period);
	),

	TP_printk("pth=%p clock_id=%d start=(%ld.%09ld) period=(%ld.%09ld)",
		  (void *)__entry->pth, __entry->clk_id,
		  __timespec_args(start),
		  __timespec_args(period)
	)
);

DEFINE_EVENT(cobalt_void, cobalt_pthread_wait_entry,
	TP_PROTO(int dummy),
	TP_ARGS(dummy)
);

TRACE_EVENT(cobalt_pthread_wait_exit,
	TP_PROTO(int status, unsigned long overruns),
	TP_ARGS(status, overruns),
	TP_STRUCT__entry(
		__field(int, status)
		__field(unsigned long, overruns)
	),
	TP_fast_assign(
		__entry->status = status;
		__entry->overruns = overruns;
	),
	TP_printk("status=%d overruns=%lu",
		  __entry->status, __entry->overruns)
);

#define cobalt_print_thread_mode(__mode)			\
	__print_flags(__mode, "|",				\
		      {PTHREAD_WARNSW, "warnsw"},		\
		      {PTHREAD_LOCK_SCHED, "lock"},		\
		      {PTHREAD_DISABLE_LOCKBREAK, "nolockbreak"})

TRACE_EVENT(cobalt_pthread_set_mode,
	TP_PROTO(int clrmask, int setmask),
	TP_ARGS(clrmask, setmask),
	TP_STRUCT__entry(
		__field(int, clrmask)
		__field(int, setmask)
	),
	TP_fast_assign(
		__entry->clrmask = clrmask;
		__entry->setmask = setmask;
	),
	TP_printk("clrmask=%#x(%s) setmask=%#x(%s)",
		  __entry->clrmask, cobalt_print_thread_mode(__entry->clrmask),
		  __entry->setmask, cobalt_print_thread_mode(__entry->setmask))
);

TRACE_EVENT(cobalt_pthread_set_name,
	TP_PROTO(unsigned long pth, const char *name),
	TP_ARGS(pth, name),
	TP_STRUCT__entry(
		__field(unsigned long, pth)
		__string(name, name)
	),
	TP_fast_assign(
		__entry->pth = pth;
		__assign_str(name, name);
	),
	TP_printk("pth=%p name=%s", (void *)__entry->pth, __get_str(name))
);

DECLARE_EVENT_CLASS(cobalt_posix_pid,
	TP_PROTO(pid_t pid),
	TP_ARGS(pid),
	TP_STRUCT__entry(
		__field(pid_t, pid)
	),
	TP_fast_assign(
		__entry->pid = pid;
	),
	TP_printk("pid=%d", __entry->pid)
);

DEFINE_EVENT(cobalt_posix_pid, cobalt_pthread_probe,
	TP_PROTO(pid_t pid),
	TP_ARGS(pid)
);

DEFINE_EVENT(cobalt_posix_pid, cobalt_pthread_stat,
	TP_PROTO(pid_t pid),
	TP_ARGS(pid)
);

TRACE_EVENT(cobalt_pthread_kill,
	TP_PROTO(unsigned long pth, int sig),
	TP_ARGS(pth, sig),
	TP_STRUCT__entry(
		__field(unsigned long, pth)
		__field(int, sig)
	),
	TP_fast_assign(
		__entry->pth = pth;
		__entry->sig = sig;
	),
	TP_printk("pth=%p sig=%d", (void *)__entry->pth, __entry->sig)
);

TRACE_EVENT(cobalt_pthread_join,
	TP_PROTO(unsigned long pth),
	TP_ARGS(pth),
	TP_STRUCT__entry(
		__field(unsigned long, pth)
	),
	TP_fast_assign(
		__entry->pth = pth;
	),
	TP_printk("pth=%p", (void *)__entry->pth)
);

TRACE_EVENT(cobalt_pthread_extend,
	TP_PROTO(unsigned long pth, const char *name),
	TP_ARGS(pth, name),
	TP_STRUCT__entry(
		__field(unsigned long, pth)
		__string(name, name)
	),
	TP_fast_assign(
		__entry->pth = pth;
		__assign_str(name, name);
	),
	TP_printk("pth=%p +personality=%s", (void *)__entry->pth, __get_str(name))
);

TRACE_EVENT(cobalt_pthread_restrict,
	TP_PROTO(unsigned long pth, const char *name),
	TP_ARGS(pth, name),
	TP_STRUCT__entry(
		__field(unsigned long, pth)
		__string(name, name)
	),
	TP_fast_assign(
		__entry->pth = pth;
		__assign_str(name, name);
	),
	TP_printk("pth=%p -personality=%s", (void *)__entry->pth, __get_str(name))
);

DEFINE_EVENT(cobalt_void, cobalt_pthread_yield,
	TP_PROTO(int dummy),
	TP_ARGS(dummy)
);

TRACE_EVENT(cobalt_sched_set_config,
	TP_PROTO(int cpu, int policy, size_t len),
	TP_ARGS(cpu, policy, len),
	TP_STRUCT__entry(
		__field(int, cpu)
		__field(int, policy)
		__field(size_t, len)
	),
	TP_fast_assign(
		__entry->cpu = cpu;
		__entry->policy = policy;
		__entry->len = len;
	),
	TP_printk("cpu=%d policy=%d(%s) len=%Zu",
		  __entry->cpu, __entry->policy,
		  cobalt_print_sched_policy(__entry->policy),
		  __entry->len)
);

TRACE_EVENT(cobalt_sched_get_config,
	TP_PROTO(int cpu, int policy, size_t rlen),
	TP_ARGS(cpu, policy, rlen),
	TP_STRUCT__entry(
		__field(int, cpu)
		__field(int, policy)
		__field(ssize_t, rlen)
	),
	TP_fast_assign(
		__entry->cpu = cpu;
		__entry->policy = policy;
		__entry->rlen = rlen;
	),
	TP_printk("cpu=%d policy=%d(%s) rlen=%Zd",
		  __entry->cpu, __entry->policy,
		  cobalt_print_sched_policy(__entry->policy),
		  __entry->rlen)
);

DECLARE_EVENT_CLASS(cobalt_posix_prio_bound,
	TP_PROTO(int policy, int prio),
	TP_ARGS(policy, prio),
	TP_STRUCT__entry(
		__field(int, policy)
		__field(int, prio)
	),
	TP_fast_assign(
		__entry->policy = policy;
		__entry->prio = prio;
	),
	TP_printk("policy=%d(%s) prio=%d",
		  __entry->policy,
		  cobalt_print_sched_policy(__entry->policy),
		  __entry->prio)
);

DEFINE_EVENT(cobalt_posix_prio_bound, cobalt_sched_min_prio,
	TP_PROTO(int policy, int prio),
	TP_ARGS(policy, prio)
);

DEFINE_EVENT(cobalt_posix_prio_bound, cobalt_sched_max_prio,
	TP_PROTO(int policy, int prio),
	TP_ARGS(policy, prio)
);

DECLARE_EVENT_CLASS(cobalt_posix_sem,
	TP_PROTO(xnhandle_t handle),
	TP_ARGS(handle),
	TP_STRUCT__entry(
		__field(xnhandle_t, handle)
	),
	TP_fast_assign(
		__entry->handle = handle;
	),
	TP_printk("sem=%#lx", __entry->handle)
);

DEFINE_EVENT(cobalt_posix_sem, cobalt_psem_wait,
	TP_PROTO(xnhandle_t handle),
	TP_ARGS(handle)
);

DEFINE_EVENT(cobalt_posix_sem, cobalt_psem_trywait,
	TP_PROTO(xnhandle_t handle),
	TP_ARGS(handle)
);

DEFINE_EVENT(cobalt_posix_sem, cobalt_psem_timedwait,
	TP_PROTO(xnhandle_t handle),
	TP_ARGS(handle)
);

DEFINE_EVENT(cobalt_posix_sem, cobalt_psem_post,
	TP_PROTO(xnhandle_t handle),
	TP_ARGS(handle)
);

DEFINE_EVENT(cobalt_posix_sem, cobalt_psem_destroy,
	TP_PROTO(xnhandle_t handle),
	TP_ARGS(handle)
);

DEFINE_EVENT(cobalt_posix_sem, cobalt_psem_broadcast,
	TP_PROTO(xnhandle_t handle),
	TP_ARGS(handle)
);

DEFINE_EVENT(cobalt_posix_sem, cobalt_psem_inquire,
	TP_PROTO(xnhandle_t handle),
	TP_ARGS(handle)
);

TRACE_EVENT(cobalt_psem_getvalue,
	TP_PROTO(xnhandle_t handle, int value),
	TP_ARGS(handle, value),
	TP_STRUCT__entry(
		__field(xnhandle_t, handle)
		__field(int, value)
	),
	TP_fast_assign(
		__entry->handle = handle;
		__entry->value = value;
	),
	TP_printk("sem=%#lx value=%d", __entry->handle, __entry->value)
);

#define cobalt_print_sem_flags(__flags)				\
  	__print_flags(__flags, "|",				\
			 {SEM_FIFO, "fifo"},			\
			 {SEM_PULSE, "pulse"},			\
			 {SEM_PSHARED, "pshared"},		\
			 {SEM_REPORT, "report"},		\
			 {SEM_WARNDEL, "warndel"},		\
			 {SEM_RAWCLOCK, "rawclock"},		\
			 {SEM_NOBUSYDEL, "nobusydel"})

TRACE_EVENT(cobalt_psem_init,
	TP_PROTO(const char *name, xnhandle_t handle,
		 int flags, unsigned int value),
	TP_ARGS(name, handle, flags, value),
	TP_STRUCT__entry(
		__string(name, name)
		__field(xnhandle_t, handle)
		__field(int, flags)
		__field(unsigned int, value)
	),
	TP_fast_assign(
		__assign_str(name, name);
		__entry->handle = handle;
		__entry->flags = flags;
		__entry->value = value;
	),
	TP_printk("sem=%#lx(%s) flags=%#x(%s) value=%u",
		  __entry->handle,
		  __get_str(name),
		  __entry->flags,
		  cobalt_print_sem_flags(__entry->flags),
		  __entry->value)
);

TRACE_EVENT(cobalt_psem_init_failed,
	TP_PROTO(const char *name, int flags, unsigned int value, int status),
	TP_ARGS(name, flags, value, status),
	TP_STRUCT__entry(
		__string(name, name)
		__field(int, flags)
		__field(unsigned int, value)
		__field(int, status)
	),
	TP_fast_assign(
		__assign_str(name, name);
		__entry->flags = flags;
		__entry->value = value;
		__entry->status = status;
	),
	TP_printk("name=%s flags=%#x(%s) value=%u error=%d",
		  __get_str(name),
		  __entry->flags,
		  cobalt_print_sem_flags(__entry->flags),
		  __entry->value, __entry->status)
);

#define cobalt_print_oflags(__flags)		\
	__print_flags(__flags,  "|", 		\
		      {O_RDONLY, "rdonly"},	\
		      {O_WRONLY, "wronly"},	\
		      {O_RDWR, "rdwr"},		\
		      {O_CREAT, "creat"},	\
		      {O_EXCL, "excl"},		\
		      {O_DIRECT, "direct"},	\
		      {O_NONBLOCK, "nonblock"},	\
		      {O_TRUNC, "trunc"})

TRACE_EVENT(cobalt_psem_open,
	TP_PROTO(const char *name, xnhandle_t handle,
		 int oflags, mode_t mode, unsigned int value),
	TP_ARGS(name, handle, oflags, mode, value),
	TP_STRUCT__entry(
		__string(name, name)
		__field(xnhandle_t, handle)
		__field(int, oflags)
		__field(mode_t, mode)
		__field(unsigned int, value)
	),
	TP_fast_assign(
		__assign_str(name, name);
		__entry->handle = handle;
		__entry->oflags = oflags;
		if (oflags & O_CREAT) {
			__entry->mode = mode;
			__entry->value = value;
		} else {
			__entry->mode = 0;
			__entry->value = 0;
		}
	),
	TP_printk("named_sem=%#lx=(%s) oflags=%#x(%s) mode=%o value=%u",
		  __entry->handle, __get_str(name),
		  __entry->oflags, cobalt_print_oflags(__entry->oflags),
		  __entry->mode, __entry->value)
);

TRACE_EVENT(cobalt_psem_open_failed,
	TP_PROTO(const char *name, int oflags, mode_t mode,
		 unsigned int value, int status),
        TP_ARGS(name, oflags, mode, value, status),
	TP_STRUCT__entry(
		__string(name, name)
		__field(int, oflags)
		__field(mode_t, mode)
		__field(unsigned int, value)
		__field(int, status)
	),
	TP_fast_assign(
		__assign_str(name, name);
		__entry->oflags = oflags;
		__entry->status = status;
		if (oflags & O_CREAT) {
			__entry->mode = mode;
			__entry->value = value;
		} else {
			__entry->mode = 0;
			__entry->value = 0;
		}
	),
	TP_printk("named_sem=%s oflags=%#x(%s) mode=%o value=%u error=%d",
		  __get_str(name),
		  __entry->oflags, cobalt_print_oflags(__entry->oflags),
		  __entry->mode, __entry->value, __entry->status)
);

DEFINE_EVENT(cobalt_posix_sem, cobalt_psem_close,
	TP_PROTO(xnhandle_t handle),
	TP_ARGS(handle)
);

TRACE_EVENT(cobalt_psem_unlink,
	TP_PROTO(const char *name),
	TP_ARGS(name),
	TP_STRUCT__entry(
		__string(name, name)
	),
	TP_fast_assign(
		__assign_str(name, name);
	),
	TP_printk("name=%s", __get_str(name))
);

#endif /* _TRACE_COBALT_POSIX_H */

/* This part must be outside protection */
#include <trace/define_trace.h>

#include <nucleus/trace.h>
#include <asm/xenomai/syscall.h>

int xntrace_max_begin(unsigned long v)
{
	return XENOMAI_SYSCALL2(__xn_sys_trace, __xntrace_op_max_begin, v);
}

int xntrace_max_end(unsigned long v)
{
	return XENOMAI_SYSCALL2(__xn_sys_trace, __xntrace_op_max_end, v);
}

int xntrace_max_reset(void)
{
	return XENOMAI_SYSCALL1(__xn_sys_trace, __xntrace_op_max_reset);
}

int xntrace_user_start(void)
{
	return XENOMAI_SYSCALL1(__xn_sys_trace, __xntrace_op_user_start);
}

int xntrace_user_stop(unsigned long v)
{
	return XENOMAI_SYSCALL2(__xn_sys_trace, __xntrace_op_user_stop, v);
}

int xntrace_user_freeze(unsigned long v, int once)
{
	return XENOMAI_SYSCALL3(__xn_sys_trace, __xntrace_op_user_freeze,
				v, once);
}

int xntrace_special(unsigned char id, unsigned long v)
{
	return XENOMAI_SYSCALL3(__xn_sys_trace, __xntrace_op_special, id, v);
}

int xntrace_special_u64(unsigned char id, unsigned long long v)
{
	return XENOMAI_SYSCALL4(__xn_sys_trace, __xntrace_op_special_u64, id,
				(unsigned long)(v >> 32),
				(unsigned long)(v & 0xFFFFFFFF));
}


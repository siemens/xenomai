#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>
#include <rtdk.h>
#include <asm-generic/current.h>

int __wrap_vfprintf(FILE *stream, const char *fmt, va_list args)
{
	if (unlikely(xeno_get_current() != XN_NO_HANDLE &&
		     !(xeno_get_current_mode() & XNRELAX)))
		return rt_vfprintf(stream, fmt, args);
	else {
		rt_print_flush_buffers();
		return __real_vfprintf(stream, fmt, args);
	}
}

int __wrap_vprintf(const char *fmt, va_list args)
{
	return __wrap_vfprintf(stdout, fmt, args);
}

int __wrap_fprintf(FILE *stream, const char *fmt, ...)
{
	va_list args;
	int rc;

	va_start(args, fmt);
	rc = __wrap_vfprintf(stream, fmt, args);
	va_end(args);

	return rc;
}

int __wrap_printf(const char *fmt, ...)
{
	va_list args;
	int rc;

	va_start(args, fmt);
	rc = __wrap_vfprintf(stdout, fmt, args);
	va_end(args);

	return rc;
}

int __wrap_fputs(const char *s, FILE *stream)
{
	if (unlikely(xeno_get_current() != XN_NO_HANDLE &&
		     !(xeno_get_current_mode() & XNRELAX)))
		return rt_fputs(s, stream);
	else {
		rt_print_flush_buffers();
		return __real_fputs(s, stream);
	}
}

int __wrap_puts(const char *s)
{
	if (unlikely(xeno_get_current() != XN_NO_HANDLE &&
		     !(xeno_get_current_mode() & XNRELAX)))
		return rt_puts(s);
	else {
		rt_print_flush_buffers();
		return __real_puts(s);
	}
}

int __wrap_fputc(int c, FILE *stream)
{
	if (unlikely(xeno_get_current() != XN_NO_HANDLE &&
		     !(xeno_get_current_mode() & XNRELAX)))
		return rt_fputc(c, stream);
	else {
		rt_print_flush_buffers();
		return __real_fputc(c, stream);
	}
}

int __wrap_putchar(int c)
{
	if (unlikely(xeno_get_current() != XN_NO_HANDLE &&
		     !(xeno_get_current_mode() & XNRELAX)))
		return rt_putchar(c);
	else {
		rt_print_flush_buffers();
		return __real_putchar(c);
	}
}

size_t __wrap_fwrite(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	if (unlikely(xeno_get_current() != XN_NO_HANDLE &&
		     !(xeno_get_current_mode() & XNRELAX)))
		return rt_fwrite(ptr, size, nmemb, stream);
	else {
		rt_print_flush_buffers();
		return __real_fwrite(ptr, size, nmemb, stream);
	}

}

void __wrap_vsyslog(int priority, const char *fmt, va_list ap)
{
	if (unlikely(xeno_get_current() != XN_NO_HANDLE &&
		     !(xeno_get_current_mode() & XNRELAX)))
		return rt_vsyslog(priority, fmt, ap);
	else {
		rt_print_flush_buffers();
		__real_vsyslog(priority, fmt, ap);
	}
}

void __wrap_syslog(int priority, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	__wrap_vsyslog(priority, fmt, args);
	va_end(args);
}

/* 
 * Checked versions for -D_FORTIFY_SOURCE
 */
int __wrap___vfprintf_chk(FILE *f, int flag, const char *fmt, va_list ap)
{
#ifdef CONFIG_XENO_FORTIFY
	if (unlikely(xeno_get_current() != XN_NO_HANDLE &&
		     !(xeno_get_current_mode() & XNRELAX)))
		return __rt_vfprintf_chk(f, flag, fmt, ap);
	else {
		rt_print_flush_buffers();
		return __real___vfprintf_chk(f, flag, fmt, ap);
	}
#else
	__wrap_fprintf(stderr, 
		       "Xenomai has to be compiled with --enable-fortify "
		      "to support applications\ncompiled with "
		      "-D_FORTIFY_SOURCE\n");
	exit(EXIT_FAILURE);
#endif
}
int __wrap___vprintf_chk(int flag, const char *fmt, va_list ap)
{
	return __wrap___vfprintf_chk(stdout, flag, fmt, ap);
}

int __wrap___fprintf_chk(FILE *f, int flag, const char *fmt, ...)
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = __wrap___vfprintf_chk(f, flag, fmt, args);
	va_end(args);

	return ret;
}

int __wrap___printf_chk(int flag, const char *fmt, ...)
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = __wrap___vprintf_chk(flag, fmt, args);
	va_end(args);

	return ret;
}

void __wrap___vsyslog_chk(int pri, int flag, const char *fmt, va_list ap)
{
#ifdef CONFIG_XENO_FORTIFY
	if (unlikely(xeno_get_current() != XN_NO_HANDLE &&
		     !(xeno_get_current_mode() & XNRELAX)))
		return __rt_vsyslog_chk(pri, flag, fmt, ap);
	else {
		rt_print_flush_buffers();
		__real___vsyslog_chk(pri, flag, fmt, ap);
	}
#else
	__wrap_fprintf(stderr, 
		       "Xenomai needs to be compiled with --enable-fortify "
		      "to support applications\ncompiled with "
		      "-D_FORTIFY_SOURCE\n");
	exit(EXIT_FAILURE);
#endif
}

void __wrap___syslog_chk(int pri, int flag, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	__wrap___vsyslog_chk(pri, flag, fmt, args);
	va_end(args);
}

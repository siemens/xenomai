#ifndef __KERNEL__

#pragma GCC system_header

#include_next <stdio.h>

#ifndef _XENO_POSIX_STDIO_H
#define _XENO_POSIX_STDIO_H

#include <stddef.h>
#include <stdarg.h>
#include <xeno_config.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int __real_vfprintf(FILE *stream, const char *fmt, va_list args);

#ifdef CONFIG_XENO_FORTIFY
int __real___vfprintf_chk(FILE *stream, int level, const char *fmt, va_list ap);
#endif

int __real_vprintf(const char *fmt, va_list args);

int __real_fprintf(FILE *stream, const char *fmt, ...);

int __real_printf(const char *fmt, ...);

int __real_puts(const char *s);

int __real_fputs(const char *s, FILE *stream);

int __real_fputc(int c, FILE *stream);

int __real_putchar(int c);

size_t __real_fwrite(const void *ptr, size_t sz, size_t nmemb, FILE *stream);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _XENO_POSIX_STDIO_H */

#endif /* !__KERNEL__ */

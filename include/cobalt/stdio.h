#ifndef __KERNEL__

#include_next <stdio.h>

#ifndef STDIO_H
#define STDIO_H

#include <stdarg.h>
#include <cobalt/wrappers.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

COBALT_DECL(int, vfprintf(FILE *stream, const char *fmt, va_list args));

COBALT_DECL(int, vprintf(const char *fmt, va_list args));

COBALT_DECL(int, fprintf(FILE *stream, const char *fmt, ...));

COBALT_DECL(int, printf(const char *fmt, ...));

COBALT_DECL(int, puts(const char *s));

COBALT_DECL(int, fputs(const char *s, FILE *stream));

#if !defined(__UCLIBC__) || !defined(__STDIO_PUTC_MACRO)

COBALT_DECL(int, fputc(int c, FILE *stream));

COBALT_DECL(int, putchar(int c));

#else

int __wrap_fputc(int c, FILE *stream);
#define __real_fputc __wrap_fputc

int __wrap_putchar(int c);
#define __real_putchar __wrap_putchar

#endif

COBALT_DECL(size_t, fwrite(const void *ptr, size_t sz, size_t nmemb, FILE *stream));

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* STDIO_H */

#endif /* !__KERNEL__ */

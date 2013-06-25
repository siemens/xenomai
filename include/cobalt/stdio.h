/*
 * Copyright (C) 2006 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */
#ifndef _COBALT_STDIO_H
#define _COBALT_STDIO_H

#pragma GCC system_header
#include_next <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <xeno_config.h>
#include <cobalt/wrappers.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

COBALT_DECL(int, vfprintf(FILE *stream, const char *fmt, va_list args));

#ifdef CONFIG_XENO_FORTIFY
COBALT_DECL(int, __vfprintf_chk(FILE *stream, int level,
				const char *fmt, va_list ap));
#endif

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

#endif /* !_COBALT_STDIO_H */

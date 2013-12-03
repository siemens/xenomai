/*
 * Copyright (C) 2011-2013 Gilles Chanteperdrix <gch@xenomai.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *  
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef POSIX_CHECK_H
#define POSIX_CHECK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define check_pthread(expr)                                             \
	({                                                              \
		int rc = (expr);                                        \
		if (rc > 0) {                                           \
			fprintf(stderr, "FAILURE %s:%d: %s: %s\n", __FILE__, __LINE__, #expr, strerror(rc)); \
			exit(EXIT_FAILURE);				\
		}                                                       \
		rc;                                                     \
	})

#define check_unix(expr)						\
	({                                                              \
		int rc = (expr);                                        \
		if (rc < 0) {                                           \
			fprintf(stderr, "FAILURE %s:%d: %s: %s\n", __FILE__, __LINE__, #expr, strerror(errno)); \
			exit(EXIT_FAILURE);				\
		}                                                       \
		rc;                                                     \
	})

#define check_mmap(expr)						\
	({                                                              \
		void *rc = (expr);					\
		if (rc == MAP_FAILED) {					\
			fprintf(stderr, "FAILURE %s:%d: %s: %s\n", __FILE__, __LINE__, #expr, strerror(errno)); \
			exit(EXIT_FAILURE);				\
		}                                                       \
		rc;                                                     \
	})

#endif /* POSIX_CHECK_H */

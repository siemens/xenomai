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

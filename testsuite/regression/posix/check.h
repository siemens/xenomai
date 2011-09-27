#ifndef POSIX_CHECK_H
#define POSIX_CHECK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define check_pthread(expr)                                             \
	({                                                              \
		int rc = (expr);                                        \
		if (rc > 0) {                                           \
			fprintf(stderr, "%s:%d: "#expr ": %s\n", __FILE__, __LINE__, strerror(rc)); \
			exit(EXIT_FAILURE);				\
		}                                                       \
		rc;                                                     \
	})

#define check_unix(expr)						\
	({                                                              \
		int rc = (expr);                                        \
		if (rc < 0) {                                           \
			fprintf(stderr, "%s:%d: "#expr ": %s\n", __FILE__, __LINE__, strerror(errno)); \
			exit(EXIT_FAILURE);				\
		}                                                       \
		rc;                                                     \
	})

#endif /* POSIX_CHECK_H */

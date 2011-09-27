#ifndef NATIVE_CHECK_H
#define NATIVE_CHECK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define check_native(expr)						\
	({                                                              \
		int rc = (expr);                                        \
		if (rc < 0) {                                           \
			fprintf(stderr, "%s:%d: "#expr ": %s\n", __FILE__, __LINE__, strerror(-rc)); \
			exit(EXIT_FAILURE);				\
		}                                                       \
		rc;                                                     \
	})

#endif /* NATIVE_CHECK_H */

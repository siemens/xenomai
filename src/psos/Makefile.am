lib_LTLIBRARIES = libpsos.la

libpsos_la_LDFLAGS = -version-info 0:0:0 -lpthread

libpsos_la_SOURCES =	\
	init.c		\
	pt.c		\
	pt.h		\
	queue.c		\
	queue.h		\
	rn.c		\
	rn.h		\
	sem.c		\
	sem.h		\
	task.c		\
	task.h		\
	tm.c		\
	tm.h		\
	reference.h

libpsos_la_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/src/include

EXTRA_DIST = testsuite

SPARSE = sparse

sparse:
	@for i in $(libpsos_la_SOURCES); do \
		$(SPARSE) $(CHECKFLAGS) $(srcdir)/$$i; \
	done

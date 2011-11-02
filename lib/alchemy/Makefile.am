lib_LTLIBRARIES = libalchemy.la

libalchemy_la_LDFLAGS = -version-info 0:0:0 -lpthread

libalchemy_la_SOURCES =	\
	init.c		\
	internal.c	\
	internal.h	\
	reference.h	\
	cond.c		\
	cond.h		\
	event.c		\
	event.h		\
	mutex.c		\
	mutex.h		\
	queue.c		\
	queue.h		\
	task.c		\
	task.h		\
	sem.c		\
	sem.h		\
	timer.c		\
	timer.h

libalchemy_la_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include

EXTRA_DIST = testsuite

SPARSE = sparse

sparse:
	@for i in $(libalchemy_la_SOURCES); do \
		$(SPARSE) $(CHECKFLAGS) $(srcdir)/$$i; \
	done

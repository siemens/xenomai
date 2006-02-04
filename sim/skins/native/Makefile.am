vpath %.c $(top_srcdir)/../ksrc/skins/native

CC = $(top_builddir)/gcic/gcic

override CFLAGS = -g

if CONFIG_XENO_MVM_ATOMIC_CORE
atomic_core := --no-inst -O2
endif

AM_CPPFLAGS = \
	-D__IN_XENO__ \
	--gcic-backend=$(prefix)/libexec/gcic \
	--skin-code \
	$(atomic_core) \
	-I$(srcdir)/../include \
	-I$(top_srcdir)/../include

lib_LIBRARIES = libnative_s.a

nodist_libnative_s_a_SOURCES = \
	module.c \
	registry.c \
	task.c \
	timer.c \
	sem.c \
	event.c \
	mutex.c \
	cond.c \
	queue.c \
	heap.c \
	alarm.c

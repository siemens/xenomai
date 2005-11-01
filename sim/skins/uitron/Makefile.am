vpath %.c $(top_srcdir)/../ksrc/skins/uitron

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
	-I$(srcdir)/../include

lib_LIBRARIES = libuitron_s.a

nodist_libuitron_s_a_SOURCES = \
	flag.c \
	mbx.c \
	module.c \
	sem.c \
	task.c

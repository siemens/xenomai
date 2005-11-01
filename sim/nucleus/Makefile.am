vpath %.c $(top_srcdir)/../ksrc/nucleus

CC = $(top_builddir)/gcic/gcic

override CFLAGS = -g

if CONFIG_XENO_MVM_ATOMIC_CORE
atomic_core := --no-inst -O2
endif

lib_LIBRARIES = libnucleus_s.a

# Shadow, pipe and pervasive supports make no sense compiling in
# native simulation mode. Therefore, their respective files are not
# included here.

nodist_libnucleus_s_a_SOURCES = \
			heap.c \
			intr.c \
			module.c \
			pod.c \
			synch.c \
			thread.c \
			timer.c

AM_CPPFLAGS = \
	-D__IN_XENO__ \
	--gcic-backend=$(prefix)/libexec/gcic \
	--kernel-code \
	$(atomic_core) \
	-I$(srcdir)/../include

libnucleus_s_a_LIBADD = ../adapter/adapter.o

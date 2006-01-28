vpath %.c $(top_srcdir)/../ksrc/skins/posix/demos

CC = $(top_builddir)/gcic/gcic

if CONFIG_XENO_MVM_ATOMIC_CORE
atomic_core := --no-inst -O2
endif

AM_CFLAGS=-Wall -g -Werror-implicit-function-declaration

INCLUDES = \
	-D__IN_XENO__ -DPRODUCER -DCONSUMER \
	--gcic-backend=$(prefix)/libexec/gcic \
	-I$(srcdir)/.. \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/../include

AM_LDFLAGS = \
	-u __xeno_user_init \
	--gcic-backend=$(prefix)/libexec/gcic \
	--cc=$(CXX) \
	--no-mvm-libs \
	../libposix_s.a \
	../../../nucleus/libnucleus_s.a \
	../../../adapter/libadapter.a \
	../../../vm/libmvm.la \
	../../../vmutils/libmvmutils.la

check_PROGRAMS=satch_s

satch_s_SOURCES=satch.c

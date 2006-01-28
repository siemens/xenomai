CC = ../../../gcic/gcic

override CFLAGS=-g

INCLUDES = \
	-D__IN_XENO__ \
	--gcic-backend=$(prefix)/libexec/gcic \
	-I$(srcdir)/.. \
	-I@XENO_INSTALLDIR@/source/include \
	-I@XENO_INSTALLDIR@/source/skins \
	-I$(top_srcdir)/include

LDFLAGS = \
	-u __xeno_user_init \
	--gcic-backend=$(prefix)/libexec/gcic \
	--cc=$(CXX) \
	--no-mvm-libs \
	libtests.a \
	../libposix_s.a \
	../../../nucleus/libnucleus_s.a \
	../../../adapter/libadapter.a \
	../../../vm/libmvm.la \
	../../../vmutils/libmvmutils.la

check_PROGRAMS= \
	tthread \
	tsem \
	tmutex \
	tcond \
	tcancel \
	tsignal \
	ttsd \
	tclock \
	tmq \
	ttimers

tthread_DEPENDENCIES= ../libposix_s.a libtests.a
tsem_DEPENDENCIES= ../libposix_s.a libtests.a
tmutex_DEPENDENCIES=../libposix_s.a libtests.a
tcond_DEPENDENCIES=../libposix_s.a libtests.a
tcancel_DEPENDENCIES=../libposix_s.a libtests.a
tsignal_DEPENDENCIES=../libposix_s.a libtests.a
ttsd_DEPENDENCIES=../libposix_s.a libtests.a
tclock_DEPENDENCIES=../libposix_s.a libtests.a
ttimers_DEPENDENCIES=../libposix_s.a libtests.a
tmq_DEPENDENCIES=../libposix_s.a libtests.a

check_LIBRARIES=libtests.a

libtests_a_SOURCES = xntest.c
libtests_a_DEPENDENCIES = xntest.h posix_test.h
tthread_SOURCES = tthread.c
tsem_SOURCES = tsem.c
tmutex_SOURCES = tmutex.c
tcond_SOURCES = tcond.c
tcancel_SOURCES = tcancel.c
tsignal_SOURCES = tsignal.c
ttsd_SOURCES = ttsd.c
tclock_SOURCES = tclock.c
tmq_SOURCES = tmq.c
ttimers_SOURCES = ttimers.c

EXTRA_DIST = skeleton.c

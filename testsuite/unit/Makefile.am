testdir = @XENO_TEST_DIR@

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

test_PROGRAMS = \
	wakeup-time \
	rtdm

if XENO_COBALT

coredep_lib = ../../lib/cobalt/libcobalt.la

test_PROGRAMS += arith \
	mutex-torture \
	cond-torture \
	check-vdso

arith_SOURCES = arith.c arith-noinline.c arith-noinline.h

arith_CPPFLAGS = \
	@XENO_USER_CFLAGS@		\
	-I$(top_srcdir)/include		\
	-Wno-missing-prototypes 

arith_LDFLAGS =

arith_LDADD = \
	../../lib/alchemy/libalchemy.la \
	../../lib/copperplate/libcopperplate.la \
	$(coredep_lib) \
	@XENO_USER_LDADD@		\
	-lpthread -lrt -lm

mutex_torture_SOURCES = mutex-torture.c

mutex_torture_CPPFLAGS =		\
	@XENO_USER_CFLAGS@		\
	-DXENO_POSIX			\
	-I$(top_srcdir)/include		\
	-Wno-missing-prototypes 

mutex_torture_LDFLAGS = $(XENO_POSIX_WRAPPERS)

mutex_torture_LDADD = \
	../../lib/alchemy/libalchemy.la \
	../../lib/copperplate/libcopperplate.la \
	$(coredep_lib) \
	@XENO_USER_LDADD@		\
	-lpthread -lrt -lm

cond_torture_SOURCES = cond-torture.c

cond_torture_CPPFLAGS =		\
	@XENO_USER_CFLAGS@		\
	-DXENO_POSIX			\
	-I$(top_srcdir)/include		\
	-Wno-missing-prototypes 

cond_torture_LDFLAGS = $(XENO_POSIX_WRAPPERS)

cond_torture_LDADD = \
	../../lib/alchemy/libalchemy.la \
	../../lib/copperplate/libcopperplate.la \
	$(coredep_lib) \
	@XENO_USER_LDADD@		\
	-lpthread -lrt -lm

check_vdso_SOURCES = check-vdso.c

check_vdso_CPPFLAGS =			\
	@XENO_USER_CFLAGS@		\
	-I$(top_srcdir)/include		\
	-Wno-missing-prototypes 

check_vdso_LDFLAGS =

check_vdso_LDADD = \
	$(coredep_lib) \
	@XENO_USER_LDADD@	\
	-lpthread -lrt -lm

else
coredep_lib =
endif

wakeup_time_SOURCES = wakeup-time.c

wakeup_time_CPPFLAGS =			\
	@XENO_USER_CFLAGS@		\
	-I$(top_srcdir)/include		\
	-Wno-missing-prototypes 

wakeup_time_LDFLAGS =

wakeup_time_LDADD = \
	../../lib/alchemy/libalchemy.la \
	../../lib/copperplate/libcopperplate.la \
	$(coredep_lib) \
	@XENO_USER_LDADD@		\
	-lpthread -lrt -lm

rtdm_SOURCES = rtdm.c

rtdm_CPPFLAGS =				\
	@XENO_USER_CFLAGS@		\
	-DXENO_POSIX			\
	-I$(top_srcdir)/include		\
	-Wno-missing-prototypes 

rtdm_LDFLAGS = $(XENO_POSIX_WRAPPERS)

rtdm_LDADD = \
	../../lib/alchemy/libalchemy.la \
	../../lib/copperplate/libcopperplate.la \
	$(coredep_lib) \
	@XENO_USER_LDADD@		\
	-lpthread -lrt -lm

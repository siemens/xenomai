testdir = @XENO_TEST_DIR@

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

test_PROGRAMS = \
	arith \
	wakeup-time \
	mutex-torture \
	cond-torture \
	check-vdso \
	rtdm

arith_SOURCES = arith.c arith-noinline.c arith-noinline.h

arith_CPPFLAGS = \
	@XENO_USER_CFLAGS@		\
	-I$(top_srcdir)/include		\
	-Wno-missing-prototypes 

arith_LDFLAGS = @XENO_USER_LDFLAGS@

arith_LDADD = \
	../../lib/alchemy/libalchemy.la \
	../../lib/copperplate/libcopperplate.la \
	../../lib/cobalt/libcobalt.la \
	-lpthread -lrt -lm

wakeup_time_SOURCES = wakeup-time.c

wakeup_time_CPPFLAGS =			\
	@XENO_USER_CFLAGS@		\
	-I$(top_srcdir)/include		\
	-Wno-missing-prototypes 

wakeup_time_LDFLAGS = @XENO_USER_LDFLAGS@

wakeup_time_LDADD = \
	../../lib/alchemy/libalchemy.la \
	../../lib/copperplate/libcopperplate.la \
	../../lib/cobalt/libcobalt.la \
	-lpthread -lrt -lm

mutex_torture_SOURCES = mutex-torture.c

mutex_torture_CPPFLAGS =		\
	@XENO_USER_CFLAGS@		\
	-DXENO_POSIX			\
	-I$(top_srcdir)/include		\
	-Wno-missing-prototypes 

mutex_torture_LDFLAGS = $(XENO_POSIX_WRAPPERS) @XENO_USER_LDFLAGS@

mutex_torture_LDADD = \
	../../lib/alchemy/libalchemy.la \
	../../lib/copperplate/libcopperplate.la \
	../../lib/cobalt/libcobalt.la \
	-lpthread -lrt -lm

cond_torture_SOURCES = cond-torture.c

cond_torture_CPPFLAGS =		\
	@XENO_USER_CFLAGS@		\
	-DXENO_POSIX			\
	-I$(top_srcdir)/include		\
	-Wno-missing-prototypes 

cond_torture_LDFLAGS = $(XENO_POSIX_WRAPPERS) @XENO_USER_LDFLAGS@

cond_torture_LDADD = \
	../../lib/alchemy/libalchemy.la \
	../../lib/copperplate/libcopperplate.la \
	../../lib/cobalt/libcobalt.la \
	-lpthread -lrt -lm

check_vdso_SOURCES = check-vdso.c

check_vdso_CPPFLAGS =			\
	@XENO_USER_CFLAGS@		\
	-I$(top_srcdir)/include		\
	-Wno-missing-prototypes 

check_vdso_LDFLAGS = @XENO_USER_LDFLAGS@

check_vdso_LDADD = \
	../../lib/cobalt/libcobalt.la \
	-lpthread -lrt -lm

rtdm_SOURCES = rtdm.c

rtdm_CPPFLAGS =				\
	@XENO_USER_CFLAGS@		\
	-DXENO_POSIX			\
	-I$(top_srcdir)/include		\
	-Wno-missing-prototypes 

rtdm_LDFLAGS = $(XENO_POSIX_WRAPPERS) @XENO_USER_LDFLAGS@

rtdm_LDADD = \
	../../lib/alchemy/libalchemy.la \
	../../lib/copperplate/libcopperplate.la \
	../../lib/cobalt/libcobalt.la \
	-lpthread -lrt -lm

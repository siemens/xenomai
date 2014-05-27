testdir = @XENO_TEST_DIR@

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

test_PROGRAMS = 	\
	rtdm

if XENO_COBALT

coredep_lib = ../../lib/cobalt/libcobalt.la

test_PROGRAMS += arith	\
	mutex-torture 	\
	cond-torture 	\
	sched-tp 	\
	sched-quota 	\
	check-vdso

arith_SOURCES = arith.c arith-noinline.c arith-noinline.h

arith_CPPFLAGS = 					\
	@XENO_USER_CFLAGS@				\
	-I$(top_srcdir)/include

arith_LDFLAGS =

arith_LDADD = 					\
	../../lib/alchemy/libalchemy.la 	\
	../../lib/copperplate/libcopperplate.la \
	$(coredep_lib) 				\
	@XENO_USER_LDADD@			\
	-lpthread -lrt -lm

mutex_torture_SOURCES = mutex-torture.c

mutex_torture_CPPFLAGS =				\
	@XENO_USER_CFLAGS@				\
	-I$(top_srcdir)/include				\
	-I$(top_srcdir)

mutex_torture_LDFLAGS = $(XENO_POSIX_WRAPPERS)

mutex_torture_LDADD = \
	../../lib/alchemy/libalchemy.la 		\
	../../lib/copperplate/libcopperplate.la 	\
	$(coredep_lib) 					\
	@XENO_USER_LDADD@				\
	-lpthread -lrt -lm

cond_torture_SOURCES = cond-torture.c

cond_torture_CPPFLAGS =					\
	@XENO_USER_CFLAGS@				\
	-I$(top_srcdir)/include

cond_torture_LDFLAGS = $(XENO_POSIX_WRAPPERS)

cond_torture_LDADD = 				\
	../../lib/alchemy/libalchemy.la 	\
	../../lib/copperplate/libcopperplate.la \
	$(coredep_lib) 				\
	@XENO_USER_LDADD@			\
	-lpthread -lrt -lm

sched_tp_SOURCES = sched-tp.c

sched_tp_CPPFLAGS =					\
	@XENO_USER_CFLAGS@				\
	-I$(top_srcdir)/include

sched_tp_LDFLAGS = $(XENO_POSIX_WRAPPERS)

sched_tp_LDADD = 				\
	../../lib/copperplate/libcopperplate.la	\
	$(coredep_lib)				\
	@XENO_USER_LDADD@			\
	-lpthread -lrt -lm

sched_quota_SOURCES = sched-quota.c

sched_quota_CPPFLAGS =				\
	@XENO_USER_CFLAGS@			\
	-I$(top_srcdir)/include

sched_quota_LDFLAGS = $(XENO_POSIX_WRAPPERS)

sched_quota_LDADD = 				\
	../../lib/copperplate/libcopperplate.la	\
	$(coredep_lib)				\
	@XENO_USER_LDADD@			\
	-lpthread -lrt -lm

check_vdso_SOURCES = check-vdso.c

check_vdso_CPPFLAGS =					\
	@XENO_USER_CFLAGS@				\
	-I$(top_srcdir)/include

check_vdso_LDFLAGS =

check_vdso_LDADD = 		\
	$(coredep_lib) 		\
	@XENO_USER_LDADD@	\
	-lpthread -lrt -lm

else
coredep_lib =
endif

rtdm_SOURCES = rtdm.c

rtdm_CPPFLAGS =						\
	@XENO_USER_CFLAGS@				\
	-I$(top_srcdir)/include

rtdm_LDFLAGS = $(XENO_POSIX_WRAPPERS)

rtdm_LDADD = 					\
	../../lib/alchemy/libalchemy.la 	\
	../../lib/copperplate/libcopperplate.la \
	$(coredep_lib) 				\
	@XENO_USER_LDADD@			\
	-lpthread -lrt -lm

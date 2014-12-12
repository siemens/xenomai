testdir = @XENO_TEST_DIR@
test_PROGRAMS = smokey

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

smokey_SOURCES = main.c

core_libs =
if XENO_COBALT
core_libs += ../../lib/cobalt/libcobalt.la
wrappers = $(XENO_POSIX_WRAPPERS)
SUBDIRS = 		\
	arith 		\
	bufp		\
	cond-torture 	\
	fork-exec	\
	iddp		\
	mutex-torture 	\
	rtdm 		\
	sched-quota 	\
	sched-tp 	\
	vdso-access 	\
	xddp		\
	sigdebug
else
SUBDIRS =
wrappers =
endif

plugin_list = $(foreach plugin,$(SUBDIRS),$(plugin)/lib$(plugin).a)
# wrap-link.sh is confused by -whole-archive, so work around
# this by forcing undefined references to symbols we expect the
# plugins to export.
sym_prefix=@XENO_SYMBOL_PREFIX@
undef_list = $(foreach plugin,$(SUBDIRS),-u $(sym_prefix)smokey_plugin_$(subst -,_,$(plugin)))

smokey_CPPFLAGS = 			\
	$(XENO_USER_CFLAGS)		\
	-I$(top_srcdir)/include

smokey_LDFLAGS=$(wrappers) -Wl,--wrap=main \
	-Wl,--dynamic-list=$(top_srcdir)/scripts/dynlist.ld $(undef_list)

smokey_LDADD = 					\
	$(plugin_list)				\
	../../lib/smokey/libsmokey.la		\
	../../lib/copperplate/libcopperplate.la	\
	$(core_libs)				\
	 @XENO_USER_LDADD@			\
	-lpthread -lrt

DIST_SUBDIRS = 		\
	arith 		\
	bufp		\
	cond-torture 	\
	fork-exec	\
	iddp		\
	mutex-torture 	\
	rtdm 		\
	sched-quota 	\
	sched-tp 	\
	vdso-access 	\
	xddp		\
	sigdebug

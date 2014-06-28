testdir = @XENO_TEST_DIR@
test_PROGRAMS = smokey

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

smokey_SOURCES = main.c

core_libs =
if XENO_COBALT
core_libs += ../../lib/cobalt/libcobalt.la
wrappers = $(XENO_POSIX_WRAPPERS)
SUBDIRS = arith sched-quota sched-tp cond-torture mutex-torture rtdm vdso-access
else
SUBDIRS =
wrappers =
endif

plugin_list = $(foreach plugin,$(SUBDIRS),$(plugin)/lib$(plugin).a)
# wrap-link.sh is confused by -whole-archive, so work around
# this by forcing undefined references to symbols we expect the
# plugins to export.
undef_list = $(foreach plugin,$(SUBDIRS),-u smokey_plugin_$(subst -,_,$(plugin)))

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

DIST_SUBDIRS = arith sched-quota sched-tp cond-torture mutex-torture rtdm vdso-access

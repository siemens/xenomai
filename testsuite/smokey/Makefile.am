testdir = @XENO_TEST_DIR@
test_PROGRAMS = smokey

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

smokey_SOURCES = main.c

COBALT_SUBDIRS = 	\
	arith 		\
	bufp		\
	cpu-affinity	\
	iddp		\
	leaks		\
	net_packet_dgram\
	net_packet_raw	\
	net_udp		\
	net_common	\
	posix-clock	\
	posix-cond 	\
	posix-fork	\
	posix-mutex 	\
	posix-select 	\
	rtdm 		\
	sched-quota 	\
	sched-tp 	\
	sigdebug	\
	timerfd		\
	tsc		\
	vdso-access 	\
	xddp

MERCURY_SUBDIRS =

DIST_SUBDIRS = $(COBALT_SUBDIRS) $(MERCURY_SUBDIRS)

if XENO_COBALT
wrappers = $(XENO_POSIX_WRAPPERS)
SUBDIRS = $(COBALT_SUBDIRS)
else
SUBDIRS = $(MERCURY_SUBDIRS)
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

smokey_LDFLAGS=$(wrappers) @XENO_AUTOINIT_LDFLAGS@ $(undef_list)

smokey_LDADD = 					\
	$(plugin_list)				\
	../../lib/smokey/libsmokey.la		\
	../../lib/copperplate/libcopperplate.la	\
	../../lib/@XENO_CORE_LIB@		\
	 @XENO_USER_LDADD@			\
	-lpthread -lrt

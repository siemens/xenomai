sbin_PROGRAMS = analogy_config analogy_calibrate

bin_PROGRAMS = \
	cmd_read \
	cmd_write \
	cmd_bits \
	insn_read \
	insn_write \
	insn_bits \
	wf_generate

CPPFLAGS = 						\
	@XENO_USER_CFLAGS@ 				\
	-ggdb						\
	-I$(top_srcdir)/include                         \
        -I$(top_srcdir)/lib/analogy

LDFLAGS =

noinst_HEADERS = wf_facilities.h analogy_calibrate.h calibration_ni_m.h

noinst_LTLIBRARIES = libwaveform.la

libwaveform_la_SOURCES = wf_facilities.c

analogy_config_SOURCES = analogy_config.c
analogy_config_LDADD = \
	../../lib/analogy/libanalogy.la \
	../../lib/cobalt/libcobalt.la	\
	@XENO_USER_LDADD@		\
	-lrt -lpthread

analogy_calibrate_SOURCES = analogy_calibrate.c calibration_ni_m.c
analogy_calibrate.c: calibration_ni_m.h
analogy_calibrate_LDADD = \
	../../lib/analogy/libanalogy.la \
	../../lib/cobalt/libcobalt.la	\
	@XENO_USER_LDADD@		\
	-lpthread -lrt -lm

cmd_read_SOURCES = cmd_read.c
cmd_read_LDADD = \
	../../lib/analogy/libanalogy.la \
	../../lib/alchemy/libalchemy.la \
	../../lib/copperplate/libcopperplate.la \
	../../lib/cobalt/libcobalt.la	\
	@XENO_USER_LDADD@		\
	-lpthread -lrt

cmd_write_SOURCES = cmd_write.c
cmd_write_LDADD = \
	../../lib/analogy/libanalogy.la \
	../../lib/alchemy/libalchemy.la \
	../../lib/copperplate/libcopperplate.la \
	../../lib/cobalt/libcobalt.la	\
	@XENO_USER_LDADD@		\
	-lpthread -lrt

cmd_bits_SOURCES = cmd_bits.c
cmd_bits_LDADD = \
	../../lib/analogy/libanalogy.la \
	../../lib/cobalt/libcobalt.la	\
	@XENO_USER_LDADD@		\
	-lpthread -lrt

insn_read_SOURCES = insn_read.c
insn_read_LDADD = \
	../../lib/analogy/libanalogy.la \
	../../lib/cobalt/libcobalt.la	\
	@XENO_USER_LDADD@		\
	-lpthread -lrt

insn_write_SOURCES = insn_write.c
insn_write_LDADD = \
	../../lib/analogy/libanalogy.la \
	../../lib/cobalt/libcobalt.la	\
	@XENO_USER_LDADD@		\
	-lpthread -lrt

insn_bits_SOURCES = insn_bits.c
insn_bits_LDADD = \
	../../lib/analogy/libanalogy.la \
	../../lib/cobalt/libcobalt.la	\
	@XENO_USER_LDADD@		\
	-lpthread -lrt

wf_generate_SOURCES = wf_generate.c
wf_generate_LDADD = ./libwaveform.la -lm


.PHONY: git-stamp


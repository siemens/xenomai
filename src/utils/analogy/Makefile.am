sbin_PROGRAMS = analogy_config

bin_PROGRAMS = \
	cmd_read \
	cmd_write \
	cmd_bits \
	insn_read \
	insn_write \
	insn_bits \
	wf_generate

CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include

LDFLAGS = \
	@XENO_USER_LDFLAGS@

noinst_HEADERS = wf_facilities.h

noinst_LTLIBRARIES = libwaveform.la

libwaveform_la_SOURCES = wf_facilities.c

analogy_config_SOURCES = analogy_config.c
analogy_config_LDADD = \
	../../drvlib/analogy/libanalogy.la \
	../../skins/rtdm/librtdm.la \
	../../skins/common/libxenomai.la \
	-lpthread -lrt

cmd_read_SOURCES = cmd_read.c
cmd_read_LDADD = \
	../../drvlib/analogy/libanalogy.la \
	../../skins/native/libnative.la \
	../../skins/rtdm/librtdm.la \
	../../skins/common/libxenomai.la \
	-lpthread -lrt

cmd_write_SOURCES = cmd_write.c
cmd_write_LDADD = \
	../../drvlib/analogy/libanalogy.la \
	../../skins/native/libnative.la \
	../../skins/rtdm/librtdm.la \
	../../skins/common/libxenomai.la \
	-lpthread -lrt

cmd_bits_SOURCES = cmd_bits.c
cmd_bits_LDADD = \
	../../drvlib/analogy/libanalogy.la \
	../../skins/rtdm/librtdm.la \
	../../skins/common/libxenomai.la \
	-lpthread -lrt

insn_read_SOURCES = insn_read.c
insn_read_LDADD = \
	../../drvlib/analogy/libanalogy.la \
	../../skins/rtdm/librtdm.la \
	../../skins/common/libxenomai.la \
	-lpthread -lrt

insn_write_SOURCES = insn_write.c
insn_write_LDADD = \
	../../drvlib/analogy/libanalogy.la \
	../../skins/rtdm/librtdm.la \
	../../skins/common/libxenomai.la \
	-lpthread -lrt

insn_bits_SOURCES = insn_bits.c
insn_bits_LDADD = \
	../../drvlib/analogy/libanalogy.la \
	../../skins/rtdm/librtdm.la \
	../../skins/common/libxenomai.la \
	-lpthread -lrt

wf_generate_SOURCES = wf_generate.c
wf_generate_LDADD = ./libwaveform.la -lm

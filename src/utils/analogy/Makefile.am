sbin_PROGRAMS = analogy_config

bin_PROGRAMS = cmd_read cmd_write insn_read

CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include

LDFLAGS = \
	@XENO_USER_LDFLAGS@ \
	-L../../drvlib/analogy \
	-L../../skins/native \
	-L../../skins/rtdm

analogy_config_SOURCES = analogy_config.c
analogy_config_LDADD = -lanalogy -lrtdm

cmd_read_SOURCES = cmd_read.c
cmd_read_LDADD = -lanalogy -lnative -lrtdm

cmd_write_SOURCES = cmd_write.c
cmd_write_LDADD = -lanalogy -lnative -lrtdm

insn_read_SOURCES = insn_read.c
insn_read_LDADD = -lanalogy -lnative -lrtdm

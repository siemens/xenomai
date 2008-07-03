sbin_PROGRAMS = comedi_config

bin_PROGRAMS = cmd_read cmd_write insn_read

CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include

LDFLAGS = \
	@XENO_USER_LDFLAGS@ \
	-L../../drvlib/comedi \
	-L../../skins/native \
	-L../../skins/rtdm

comedi_config_SOURCES = comedi_config.c
comedi_config_LDADD = -lcomedi -lrtdm

cmd_read_SOURCES = cmd_read.c
cmd_read_LDADD = -lcomedi -lnative -lrtdm

cmd_write_SOURCES = cmd_write.c
cmd_write_LDADD = -lcomedi -lnative -lrtdm

insn_read_SOURCES = insn_read.c
insn_read_LDADD = -lcomedi -lnative -lrtdm


lib_LIBRARIES = libnucleus_uvm.a

nodist_libnucleus_uvm_a_SOURCES = \
			heap.c \
			intr.c \
			module.c \
			pod.c \
			synch.c \
			thread.c \
			timer.c

AM_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include \
	-D__XENO_UVM__

CLEANFILES = $(nodist_libnucleus_uvm_a_SOURCES)

$(nodist_libnucleus_uvm_a_SOURCES): Makefile
	$(LN_S) -f $(top_srcdir)/ksrc/nucleus/$@

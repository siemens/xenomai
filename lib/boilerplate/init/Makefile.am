noinst_LIBRARIES = libbootstrap.a

libbootstrap_a_SOURCES = bootstrap.c

libbootstrap_a_CPPFLAGS =		\
	@XENO_USER_CFLAGS@		\
	-I$(top_srcdir)/include		\
	-I$(top_srcdir)/lib

all-local: $(libbootstrap_a_OBJECTS)
	cp $(libbootstrap_a_OBJECTS) bootstrap.o

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(libdir)/xenomai
	$(INSTALL_DATA) $(libbootstrap_a_OBJECTS) $(DESTDIR)$(libdir)/xenomai/bootstrap.o

uninstall-local:
	$(RM) -r $(DESTDIR)$(libdir)/xenomai

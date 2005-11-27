ACLOCAL_AMFLAGS=-I config

SUBDIRS = src include config doc scripts

EXTRA_DIST = CREDITS README.INSTALL TROUBLESHOOTING ksrc

dist-hook:
	find $(distdir)/ksrc -name .svn -depth -exec rm -fr \{\} \;

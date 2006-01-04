ACLOCAL_AMFLAGS=-I config

SUDO=sudo

SUBDIRS = src include config @XENO_MAYBE_DOCDIR@ scripts

EXTRA_DIST = CREDITS README.INSTALL TROUBLESHOOTING ksrc

dist-hook:
	rm -fr `find $(distdir) -name '.svn' -o -name CVS  -o -name '.#*' \
                -o -name '*~' -o -name autom4te.cache`
	test -e $(srcdir)/doc || rm -rf $(distdir)/doc

dev devices:
	@if test x`id -u` = x0; then \
           sudo=; \
        else if test \! "x`type $(SUDO)`" = x; then \
           echo "sudo found (sudo is $(SUDO)), testing it..."; \
           $(SUDO) true && sudo="$(SUDO)" || { \
echo '*** Warning: you are not super-user and sudo does not work, real-time\
devices'; \
echo '*** nodes will not be created.'; \
echo '*** If you do not create these devices by hand, Xenomai test programs\
will not'; \
echo '*** work.'; \
echo '*** To create these devices please run "make devices" with super-user'; \
echo '*** privileges.'; \
             exit 0; }; \
           echo sudo is working.; \
           fi; \
        fi; \
        if test -r $(DESTDIR)/etc/udev/udev.rules; then \
           for f in $(srcdir)/ksrc/nucleus/udev/*.rules; do \
           b=`basename $$f` ; \
           grep -q Xenomai:`basename $$b .rules` $(DESTDIR)/etc/udev/udev.rules || \
           $$sudo sh -c "( echo ; cat $$f ) >> $(DESTDIR)/etc/udev/udev.rules"; \
           done; \
	fi; \
        $(mkinstalldirs) $(DESTDIR)/dev; \
	for n in `seq 0 31`; do \
           f="$(DESTDIR)/dev/rtp$$n"; \
           if test \! -c $$f; then \
              $$sudo mknod -m 666 $$f c 150 $$n; \
           fi; \
        done ; \
        if test \! -c $(DESTDIR)/dev/rtheap; then \
          $$sudo mknod -m 666 $(DESTDIR)/dev/rtheap c 10 254; \
        fi

install-exec-local: devices

install-nodev:
	$(MAKE) SUDO=false install

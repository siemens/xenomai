ACLOCAL_AMFLAGS = -I m4

testdir = @XENO_TEST_DIR@
pkgdir = $(pkgdatadir)

bin_SCRIPTS = xeno-regression-test

xeno-regression-test: $(srcdir)/xeno-regression-test.in Makefile
	sed "s,@testdir@,$(testdir),;s,@pkgdir@,$(pkgdir)," $< > $@

SUBDIRS = native posix native+posix

EXTRA_DIST = xeno-regression-test.in

CLEANFILES = xeno-regression-test

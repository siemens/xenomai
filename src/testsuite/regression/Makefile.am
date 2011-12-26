ACLOCAL_AMFLAGS = -I m4

testdir = @XENO_TEST_DIR@

bin_SCRIPTS = xeno-regression-test

xeno-regression-test: $(srcdir)/xeno-regression-test.in Makefile
	sed "s,@testdir@,$(testdir)," $< > $@

SUBDIRS = native posix native+posix

EXTRA_DIST = xeno-regression-test.in

CLEANFILES = xeno-regression-test

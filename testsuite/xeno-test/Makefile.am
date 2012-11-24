testdir = @XENO_TEST_DIR@
pkgdir = $(pkgdatadir)

test_SCRIPTS = xeno-test-run-wrapper dohell
test_PROGRAMS = xeno-test-run
bin_SCRIPTS = xeno-test

xeno_test_run_CPPFLAGS = -DTESTDIR=\"$(testdir)\" -D_GNU_SOURCE
xeno_test_run_LDADD = -lpthread -lrt

xeno-test: $(srcdir)/xeno-test.in Makefile
	sed "s,@testdir@,$(testdir),;s,@pkgdir@,$(pkgdir)," $< > $@

EXTRA_DIST = $(test_SCRIPTS) xeno-test.in

CLEANFILES = xeno-test

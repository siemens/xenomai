testdir = @XENO_TEST_DIR@

test_SCRIPTS = xeno-test-run-wrapper dohell
test_PROGRAMS = xeno-test-run
bin_SCRIPTS = xeno-test

xeno_test_run_CPPFLAGS = -DTESTDIR=\"$(testdir)\"

xeno-test: $(srcdir)/xeno-test.in Makefile
	sed "s,@testdir@,$(testdir)," $< > $@

EXTRA_DIST = $(test_SCRIPTS) xeno-test.in

CLEANFILES = xeno-test

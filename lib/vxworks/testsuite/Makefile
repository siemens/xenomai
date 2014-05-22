ifeq ($(DESTDIR),)
XENO_CONFIG=xeno-config
else
XENO_CONFIG=$(DESTDIR)/bin/xeno-config
endif

prefix := $(shell DESTDIR=$(DESTDIR) $(XENO_CONFIG) --prefix)
solibs := $(shell DESTDIR=$(DESTDIR) $(XENO_CONFIG) --libdir)

ifeq ($(prefix),)
$(error Please add <xenomai-install-path>/bin to your PATH variable or specify DESTDIR)
endif

TESTS := task-1 task-2 msgQ-1 msgQ-2 msgQ-3 wd-1 sem-1 sem-2 sem-3 sem-4 lst-1 rng-1

CFLAGS := $(shell DESTDIR=$(DESTDIR) $(XENO_CONFIG) --skin=vxworks --cflags) -g
LDFLAGS := $(shell DESTDIR=$(DESTDIR) $(XENO_CONFIG) --skin=vxworks --ldflags)
CC = $(shell DESTDIR=$(DESTDIR) $(XENO_CONFIG) --cc)

all: $(TESTS)

%: %.c
	$(CC) -o $@ $< $(CFLAGS) $(LDFLAGS)

install: all
	install -d $(prefix)/testsuite/vxworks
	install -t $(prefix)/testsuite/vxworks $(TESTS)

clean:
	$(RM) $(TESTS) *~

# Run the test suite. We pin all tests to CPU #0, so that SMP does not
# alter the execution sequence we expect from them.
test: all
	@for t in $(TESTS); do \
		echo -n $$t...; \
		sudo LD_LIBRARY_PATH=$(solibs) $(VALGRIND) ./$$t --cpu-affinity=0 --silent && echo ok || echo BAD; \
	done

test/%: %
	sudo LD_LIBRARY_PATH=$(solibs) $(VALGRIND) ./$(@F) --cpu-affinity=0 --silent && echo ok || echo BAD

.PHONY: clean test

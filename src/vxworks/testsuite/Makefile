ifeq ($(DESTDIR),)
XENO_CONFIG=xeno-config
else
XENO_CONFIG=$(DESTDIR)/bin/xeno-config
endif

prefix := $(shell DESTDIR=$(DESTDIR) $(XENO_CONFIG) --prefix)

ifeq ($(prefix),)
$(error Please add <xenomai-install-path>/bin to your PATH variable or specify DESTDIR)
endif

TESTS := task-1 task-2 msgQ-1 msgQ-2 msgQ-3 wd-1 sem-1 sem-2 sem-3 sem-4 lst-1 rng-1

CFLAGS := $(shell DESTDIR=$(DESTDIR) $(XENO_CONFIG) --skin=vxworks --cflags) -g
LDFLAGS := $(shell DESTDIR=$(DESTDIR) $(XENO_CONFIG) --skin=vxworks --ldflags)
CCLD = $(shell DESTDIR=$(DESTDIR) $(XENO_CONFIG) --ccld)

all: $(TESTS)

%: %.c
	$(CCLD) -o $@ $< $(CFLAGS) $(LDFLAGS)

clean:
	$(RM) $(TESTS) *~

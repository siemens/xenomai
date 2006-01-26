# Using a single source image, this demo illustrates the real-time
# guarantees provided by Xenomai's POSIX skin in user-space
# (accuracy_rt) compared to those available from the native
# NPTL/LinuxThreads supports (accuracy). You need the xeno_hal.ko +
# xno_nucleus.ko + xeno_posix.ko modules to be loaded to run the
# accuracy_rt test.

all:

bin_PROGRAMS= accuracy accuracy_rt satch_rt
lib_LIBRARIES=

CPPFLAGS= -D_GNU_SOURCE -D_REENTRANT
CFLAGS= -Wstrict-prototypes -g -O2 -Wall
LDFLAGS= -lpthread -lrt

accuracy_rt.o: CPPFLAGS=-DSPERIOD=100

accuracy.o: CPPFLAGS+=-DSPERIOD=2000

satch_rt.o: CPPFLAGS=-DCONSUMER

# To use this makefile with xeno-config not located in PATH, type :
# make XENO_CONFIG=/path/to/xeno-config
XENO_CONFIG=xeno-config
prefix := $(shell $(XENO_CONFIG) --prefix)

ifeq ($(prefix),)
$(error Please add <xenomai-install-path>/bin to your PATH variable or type: \
make XENO_CONFIG=<xenomai-install-path>/bin/xeno-config)
endif

CC:= $(shell $(XENO_CONFIG) --cc)

CFLAGS_RT:= $(shell $(XENO_CONFIG) --posix-cflags) -g
LDFLAGS_RT:= $(shell $(XENO_CONFIG) --posix-ldflags)

all: $(bin_PROGRAMS) $(lib_LIBRARIES)

%_rt.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS_RT) $< -c -o $@

%_rt: LDFLAGS=$(LDFLAGS_RT)

clean:
	$(RM) $(bin_PROGRAMS) $(lib_LIBRARIES) *.o

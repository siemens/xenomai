XENO_CONFIG=xeno-config
prefix := $(shell $(XENO_CONFIG) --prefix)

ifeq ($(prefix),)
$(error Please add <xenomai-install-path>/bin to your PATH variable)
endif

CC:= $(shell $(XENO_CONFIG) --cc)
CFLAGS_STD= -D_GNU_SOURCE -D_REENTRANT -Wstrict-prototypes -O2 -Wall -DSPERIOD=2000
LDFLAGS_STD= -lpthread -lrt

CFLAGS_RT:= $(shell $(XENO_CONFIG) --posix-cflags) -DSPERIOD=100
LDFLAGS_RT:= $(shell $(XENO_CONFIG) --posix-ldflags)

# Using a single source image, this demo illustrates the real-time
# guarantees provided by Xenomai's POSIX skin in user-space
# (accuracy_rt) compared to those available from the native
# NPTL/LinuxThreads supports (accuracy). You need the xeno_hal.ko +
# xno_nucleus.ko + xeno_posix.ko modules to be loaded to run the
# accuracy_rt test.

EXES = accuracy accuracy_rt satch_rt

all: $(EXES)

accuracy: accuracy.c
	$(CC) $(CFLAGS_STD) $< -o $@ $(LDFLAGS_STD)

accuracy_rt: accuracy.c
	$(CC) $(CFLAGS_RT) $< -o $@ $(LDFLAGS_RT)

satch_rt: satch.c
	$(CC) $(CFLAGS_RT) $< -o $@ $(LDFLAGS_RT)

clean:
	$(RM) $(EXES)

# Allow overriding xeno-config on make command line
XENO_CONFIG=xeno-config
prefix := $(shell $(XENO_CONFIG) --prefix)

ifeq ($(prefix),)
$(error Please add <xenomai-install-path>/bin to your PATH variable)
endif

STD_CFLAGS  := $(shell $(XENO_CONFIG) --xeno-cflags)
STD_LDFLAGS := $(shell $(XENO_CONFIG) --xeno-ldflags) -lnative
STD_TARGETS := satch latency

GCIC := $(prefix)/bin/gcic
SIM_CFLAGS  := -g
SIM_LDFLAGS := -lnative_s
SIM_TARGETS := satch_s latency_s

# "std" makes all demos running natively in user-space.
# "sim" makes all demos running over the RTOS simulator.
# "all" makes both.

std: $(STD_TARGETS)

sim: $(SIM_TARGETS)

all: std sim

$(STD_TARGETS): $(STD_TARGETS:%=%.c)
	$(CC) -o $@ $< $(STD_CFLAGS) $(STD_LDFLAGS)

$(SIM_TARGETS): $(SIM_TARGETS:%_s=%_s.o)
	$(GCIC) -o $@ $^ $(SIM_LDFLAGS)

%_s.o: %.c
	$(GCIC) -o $@ -c $< $(SIM_CFLAGS)

clean:
	$(RM) -f *.o *_s.o $(STD_TARGETS) $(SIM_TARGETS)

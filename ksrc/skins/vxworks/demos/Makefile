XENO_CONFIG := xeno-config
prefix := $(shell $(XENO_CONFIG) --prefix)

ifeq ($(prefix),)
$(error Please add <xenomai-install-path>/bin to your PATH variable)
endif

UVM_CFLAGS  := $(shell $(XENO_CONFIG) --uvm-cflags)
UVM_LDFLAGS := $(shell $(XENO_CONFIG) --uvm-ldflags) -lvxworks
UVM_TARGETS := satch koan

GCIC := $(prefix)/bin/gcic
SIM_CFLAGS  := -g
SIM_LDFLAGS := -lvxworks_s
SIM_TARGETS := satch_s koan_s

# "uvm" makes all demos running over the UVM.
# "sim" makes all demos running over the RTOS simulator.
# "all" makes both.

uvm: $(UVM_TARGETS)

sim: $(SIM_TARGETS)

all: uvm sim

satch: satch.c
	$(CC) -o $@ $< $(UVM_CFLAGS) $(UVM_LDFLAGS)

satch_s: satch_s.o
	$(GCIC) -o $@ $< $(SIM_LDFLAGS)

koan: koan.c
	$(CC) -o $@ $< $(UVM_CFLAGS) $(UVM_LDFLAGS) -lnative

koan_s: koan_s.o
	$(GCIC) -o $@ $< $(SIM_LDFLAGS) -lnative_s

%_s.o: %.c
	$(GCIC) -o $@ -c $< $(SIM_CFLAGS)

clean:
	$(RM) -f *.o *_s.o $(UVM_TARGETS) $(SIM_TARGETS)

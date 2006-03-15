XENO_CONFIG := xeno-config
prefix := $(shell $(XENO_CONFIG) --prefix)

ifeq ($(prefix),)
$(error Please add <xenomai-install-path>/bin to your PATH variable)
endif

UVM_CFLAGS  := $(shell $(XENO_CONFIG) --uvm-cflags)
UVM_LDFLAGS := $(shell $(XENO_CONFIG) --uvm-ldflags) -lvxworks_uvm
UVM_TARGETS := satch_uvm koan_uvm

GCIC := $(prefix)/bin/gcic
SIM_CFLAGS  := -g
SIM_LDFLAGS := -lvxworks_sim
SIM_TARGETS := satch_sim koan_sim

# "uvm" makes all demos running over the UVM.
# "sim" makes all demos running over the RTOS simulator.
# "all" makes both.

uvm: $(UVM_TARGETS)

sim: $(SIM_TARGETS)

all: uvm sim

satch_uvm: satch.c
	$(CC) -o $@ $< $(UVM_CFLAGS) $(UVM_LDFLAGS)

satch_sim: satch_sim.o
	$(GCIC) -o $@ $< $(SIM_LDFLAGS)

koan_uvm: koan.c
	$(CC) -o $@ $< $(UVM_CFLAGS) $(UVM_LDFLAGS)

koan_sim: koan_sim.o
	$(GCIC) -o $@ $< $(SIM_LDFLAGS) -lnative_sim

%_sim.o: %.c
	$(GCIC) -o $@ -c $< $(SIM_CFLAGS)

clean:
	$(RM) -f *.o *_sim.o $(UVM_TARGETS) $(SIM_TARGETS)

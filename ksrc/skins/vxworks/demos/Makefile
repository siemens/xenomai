XENO_CONFIG := xeno-config
prefix := $(shell $(XENO_CONFIG) --prefix)

ifeq ($(prefix),)
$(error Please add <xenomai-install-path>/bin to your PATH variable)
endif

STD_CFLAGS  := $(shell $(XENO_CONFIG) --xeno-cflags)
STD_LDFLAGS := $(shell $(XENO_CONFIG) --xeno-ldflags) -lvxworks
STD_TARGETS := satch

UVM_CFLAGS  := $(shell $(XENO_CONFIG) --uvm-cflags)
UVM_LDFLAGS := $(shell $(XENO_CONFIG) --uvm-ldflags) -lvxworks_uvm
UVM_TARGETS := satch_uvm koan_uvm

GCIC := $(prefix)/bin/gcic
SIM_CFLAGS  := -g
SIM_LDFLAGS := -lvxworks_sim
SIM_TARGETS := satch_sim koan_sim

# "std" makes all demos running natively in user-space.
# "uvm" makes all demos running over the UVM.
# "sim" makes all demos running over the simulator.
# "all" makes, well... all.

std: $(STD_TARGETS)

uvm: $(UVM_TARGETS)

sim: $(SIM_TARGETS)

all: std uvm sim

satch: satch.c
	$(CC) -o $@ $< $(STD_CFLAGS) $(STD_LDFLAGS)

satch_uvm: satch.c
	$(CC) -o $@ $< $(UVM_CFLAGS) $(UVM_LDFLAGS)

satch_sim: satch_sim.o
	$(GCIC) -o $@ $< $(SIM_LDFLAGS)

koan: koan.c
	$(CC) -o $@ $< $(STD_CFLAGS) $(STD_LDFLAGS) -lnative

koan_uvm: koan.c
	$(CC) -o $@ $< $(UVM_CFLAGS) $(UVM_LDFLAGS) -lnative

koan_sim: koan_sim.o
	$(GCIC) -o $@ $< $(SIM_LDFLAGS) -lnative_sim

%_sim.o: %.c
	$(GCIC) -o $@ -c $< $(SIM_CFLAGS)

clean:
	$(RM) -f *.o *_sim.o $(STD_TARGETS) $(UVM_TARGETS) $(SIM_TARGETS)

prefix := $(shell xeno-config --prefix)

ifeq ($(prefix),)
$(error Please add <xenomai-install-path>/bin to your PATH variable)
endif

UVM_CFLAGS  := $(shell xeno-config --uvm-cflags)
UVM_LDFLAGS := $(shell xeno-config --uvm-ldflags) -lpsos
UVM_TARGETS := satch

GCIC := $(prefix)/bin/gcic
SIM_CFLAGS  := -g
SIM_LDFLAGS := -lpsos_s
SIM_TARGETS := satch_s

# "uvm" makes all demos running over the UVM.
# "sim" makes all demos running over the RTOS simulator.
# "all" makes both.

uvm: $(UVM_TARGETS)

sim: $(SIM_TARGETS)

all: uvm sim

$(UVM_TARGETS): $(UVM_TARGETS:%=%.c)
	$(CC) -o $@ $< $(UVM_CFLAGS) $(UVM_LDFLAGS)

$(SIM_TARGETS): $(SIM_TARGETS:%_s=%_s.o)
	$(GCIC) -o $@ $< $(SIM_LDFLAGS)

%_s.o: %.c
	$(GCIC) -o $@ -c $< $(SIM_CFLAGS)

clean:
	$(RM) -f *.o *_s.o $(UVM_TARGETS) $(SIM_TARGETS)

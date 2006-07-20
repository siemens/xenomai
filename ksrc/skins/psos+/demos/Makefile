prefix := $(shell xeno-config --prefix)

ifeq ($(prefix),)
$(error Please add <xenomai-install-path>/bin to your PATH variable)
endif

GCIC := $(prefix)/bin/gcic
SIM_CFLAGS  := -g
SIM_LDFLAGS := -lpsos_sim
SIM_TARGETS := satch_sim

sim: $(SIM_TARGETS)

all: sim

$(SIM_TARGETS): $(SIM_TARGETS:%_sim=%_sim.o)
	$(GCIC) -o $@ $< $(SIM_LDFLAGS)

%_sim.o: %.c
	$(GCIC) -o $@ -c $< $(SIM_CFLAGS)

clean:
	$(RM) -f *.o *_sim.o $(SIM_TARGETS)

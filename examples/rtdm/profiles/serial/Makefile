###### CONFIGURATION ######

### List of applications to be build
APPLICATIONS = cross-link

### Note: to override the search path for the xeno-config script, use "make XENO=..."


### List of modules to be build
MODULES =

### Default to sources of currently running kernel
KSRC ?= /lib/modules/$(shell uname -r)/build

### Note: to override the kernel source path, use "make KSRC=..."



###### USER SPACE BUILD (no change required normally) ######
ifneq ($(APPLICATIONS),)

XENOCONFIG=$(shell PATH=$(XENO):$(XENO)/bin:$(PATH) which xeno-config 2>/dev/null)

### Sanity check
ifeq ($(XENOCONFIG),)
all::
	@echo ">>> Invoke make like this: \"make XENO=/path/to/xeno-config\" <<<"
	@echo
endif


CFLAGS=$(shell $(XENOCONFIG) --xeno-cflags) $(MY_CFLAGS)

LDFLAGS=$(shell $(XENOCONFIG) --xeno-ldflags) $(MY_LDFLAGS) -lnative -lrtdm

all:: $(APPLICATIONS)

clean::
	$(RM) $(APPLICATIONS) *.o

endif



###### KERNEL MODULE BUILD (no change required normally) ######
ifneq ($(MODULES),)

OBJS     := ${patsubst %, %.o, $(MODULES)}
CLEANMOD := ${patsubst %, .%*, $(MODULES)}

# Kernel 2.6
ifeq ($(findstring 2.6,$(KSRC)),2.6)

obj-m        := $(OBJS)
EXTRA_CFLAGS := -I$(KSRC)/include/xenomai -I$(KSRC)/include/xenomai/posix $(ADD_CFLAGS)
PWD          := $(shell pwd)

all::
	$(MAKE) -C $(KSRC) SUBDIRS=$(PWD) modules

# Kernel 2.4
else

INCLUDE := -I$(KSRC)/include -I$(KSRC)/include/xenomai -I$(KSRC)/include/xenomai/compat -I$(KSRC)/include/xenomai/posix
CFLAGS  += -O2 -Wall -Wstrict-prototypes -Wno-trigraphs -DMODULE -D__KERNEL__ -DLINUX \
           -fno-strict-aliasing -fno-common $(INCLUDE) $(ADD_CFLAGS)

all:: $(OBJS)

endif

clean::
	$(RM) $(CLEANMOD) *.cmd *.o *.ko *.mod.c Module*.symvers
	$(RM) -R .tmp*

endif

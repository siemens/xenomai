EXTRA_AFLAGS += -Iarch/ia64/kernel

obj-$(CONFIG_XENOMAI) += xeno_hal.o

xeno_hal-y := hal.o switch.o

clean-files := $(patsubst $(obj)/%,%,		\
		$(wildcard $(obj)/*.o)		\
		$(wildcard $(obj)/.*.d)		\
		$(wildcard $(obj)/.*.cmd))

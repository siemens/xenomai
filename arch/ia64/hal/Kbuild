EXTRA_CFLAGS += -I$(xeno_srctree)/include \
		-I$(src)/../../../include \
		-I$(src)/../../.. \
		$(xeno_extradef)

EXTRA_AFLAGS += -I$(xeno_srctree)/include \
		-I$(src)/../../../include \
		-I$(src)/../../.. \
		-Iarch/ia64/kernel \
		$(xeno_extradef)

obj-m += xeno_hal.o

xeno_hal-objs := $(xeno_objs)

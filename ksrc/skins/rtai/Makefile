ifeq ($(PATCHLEVEL),6)

# Makefile frag for Linux v2.6

obj-$(CONFIG_XENO_SKIN_RTAI) += xeno_rtai.o

xeno_rtai-y := intr.o module.o task.o timer.o

xeno_rtai-$(CONFIG_XENO_OPT_PERVASIVE) += syscall.o

xeno_rtai-$(CONFIG_XENO_OPT_RTAI_FIFO) += fifo.o

xeno_rtai-$(CONFIG_XENO_OPT_RTAI_SEM) += sem.o

xeno_rtai-$(CONFIG_XENO_OPT_RTAI_SHM) += shm.o

else

# Makefile frag for Linux v2.4

O_TARGET := xeno_rtai.o

obj-y := intr.o module.o task.o timer.o

obj-$(CONFIG_XENO_OPT_PERVASIVE) += syscall.o

obj-$(CONFIG_XENO_OPT_RTAI_FIFO) += fifo.o

obj-$(CONFIG_XENO_OPT_RTAI_SEM) += sem.o

obj-$(CONFIG_XENO_OPT_RTAI_SHM) += shm.o

export-objs := $(obj-y) $(obj-m)

EXTRA_CFLAGS += -I$(TOPDIR)/include/xenomai/compat

include $(TOPDIR)/Rules.make

endif

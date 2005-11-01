
obj-$(CONFIG_XENO_SKIN_PSOS) += xeno_psos.o

xeno_psos-y := module.o asr.o event.o pt.o queue.o rn.o task.o tm.o sem.o

clean-files := $(patsubst $(obj)/%,%,		\
		$(wildcard $(obj)/*.o)		\
		$(wildcard $(obj)/.*.d)		\
		$(wildcard $(obj)/.*.cmd)	\
		$(wildcard $(obj)/*.mod.c)	\
		$(wildcard $(obj)/*.ko))

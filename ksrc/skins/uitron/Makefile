
obj-$(CONFIG_XENO_SKIN_UITRON) += xeno_uitron.o

xeno_uitron-y := flag.o mbx.o module.o sem.o task.o

clean-files := $(patsubst $(obj)/%,%,		\
		$(wildcard $(obj)/.*.d)		\
		$(wildcard $(obj)/*.o)		\
		$(wildcard $(obj)/.*.cmd)	\
		$(wildcard $(obj)/*.mod.c)	\
		$(wildcard $(obj)/*.ko))

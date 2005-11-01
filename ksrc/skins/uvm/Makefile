
obj-$(CONFIG_XENO_SKIN_UVM) += xeno_uvm.o

xeno_uvm-y := module.o syscall.o

clean-files := $(patsubst $(obj)/%,%,		\
		$(wildcard $(obj)/*.o)		\
		$(wildcard $(obj)/.*.d)		\
		$(wildcard $(obj)/.*.cmd)	\
		$(wildcard $(obj)/*.mod.c)	\
		$(wildcard $(obj)/*.ko))

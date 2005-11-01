obj-$(CONFIG_XENOMAI) += native/ posix/ psos+/ rtai/ rtdm/ vrtx/ vxworks/ uitron/ uvm/

clean-files := $(patsubst $(obj)/%,%,		\
		$(wildcard $(obj)/*.o)		\
		$(wildcard $(obj)/.*.cmd))

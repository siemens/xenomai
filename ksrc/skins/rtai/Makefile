
obj-$(CONFIG_XENO_SKIN_RTAI) += xeno_rtai.o

xeno_rtai-y := intr.o module.o task.o timer.o

xeno_rtai-$(CONFIG_XENO_OPT_PERVASIVE) += syscall.o

xeno_rtai-$(CONFIG_XENO_OPT_RTAI_FIFO) += fifo.o

xeno_rtai-$(CONFIG_XENO_OPT_RTAI_SEM) += sem.o

xeno_rtai-$(CONFIG_XENO_OPT_RTAI_SHM) += shm.o

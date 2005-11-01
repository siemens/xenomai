
menu "Real-time sub-system"

config XENOMAI
	bool "Xenomai"

source "arch/@LINUX_ARCH@/xenomai/Kconfig"

endmenu

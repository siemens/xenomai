
menu "Real-time sub-system"

config XENOMAI
	bool "Xenomai"
	default y

source "arch/@LINUX_ARCH@/xenomai/Kconfig"

endmenu

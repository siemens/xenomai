
menu "Real-time sub-system"

config XENOMAI
	bool "Xenomai"
	default y
        select IPIPE

        help
          Xenomai is a real-time extension to the Linux kernel. Note 
          that Xenomai relies on Adeos interrupt pipeline (CONFIG_IPIPE 
          option) to be enabled, so enabling this option selects the
          CONFIG_IPIPE option.

source "arch/@LINUX_ARCH@/xenomai/Kconfig"

endmenu

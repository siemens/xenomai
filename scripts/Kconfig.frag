
menu "Real-time sub-system"

comment "WARNING! You enabled APM, CPU Frequency scaling or ACPI 'processor'"
	depends on APM || CPU_FREQ || ACPI_PROCESSOR
comment "option. These options are known to cause troubles with Xenomai."
	depends on APM || CPU_FREQ || ACPI_PROCESSOR

comment "NOTE: Xenomai conflicts with PC speaker support."
	depends on !X86_TSC && X86 && INPUT_PCSPKR
comment "(menu Device Drivers/Input device support/Miscellaneous devices)"
	depends on !X86_TSC && X86 && INPUT_PCSPKR

config XENOMAI
	depends on (X86_TSC || !X86 || !INPUT_PCSPKR)
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

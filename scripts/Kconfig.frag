menuconfig XENOMAI
	depends on X86_TSC || !X86
	bool "Xenomai"
	select IPIPE
	default y
	help
	  Xenomai is a real-time extension to the Linux kernel. Note
	  that Xenomai relies on Adeos interrupt pipeline (CONFIG_IPIPE
	  option) to be enabled, so enabling this option selects the
	  CONFIG_IPIPE option.

if XENOMAI
source "arch/@LINUX_ARCH@/xenomai/Kconfig"
endif

if APM || CPU_FREQ || ACPI_PROCESSOR || INTEL_IDLE
comment "WARNING! You enabled APM, CPU Frequency scaling, ACPI 'processor'"
comment "or Intel cpuidle option. These options are known to cause troubles"
comment "with Xenomai, disable them."
endif

if !GENERIC_CLOCKEVENTS
comment "NOTE: Xenomai 3.x requires CONFIG_GENERIC_CLOCKEVENTS"
endif

config XENO_VERSION_MAJOR
       int
       default @VERSION_MAJOR@

config XENO_VERSION_MINOR
       int
       default @VERSION_MINOR@

config XENO_REVISION_LEVEL
       int
       default @REVISION_LEVEL@

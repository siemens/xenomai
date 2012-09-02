config XENOMAI
	depends on GENERIC_CLOCKEVENTS
	depends on X86_TSC || !X86
	bool "Xenomai"
	select IPIPE
	select IPIPE_WANT_APIREV_2 if IPIPE_CORE
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
comment "NOTE: Xenomai needs CONFIG_GENERIC_CLOCKEVENTS"
endif

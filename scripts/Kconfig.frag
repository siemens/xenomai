menuconfig XENOMAI
	depends on X86_TSC || !X86
	bool "Xenomai/cobalt"
	select IPIPE
	select IPIPE_WANT_APIREV_2
	default y
	help
	  Xenomai's Cobalt core is a real-time extension to the Linux
	  kernel, which exhibits very short interrupt and scheduling
	  latency, without affecting the regular kernel services.

	  This option enables the set of extended kernel services
	  required to run the real-time applications in user-space,
	  over the Xenomai libraries.

	  Please visit http://xenomai.org for more information.

if XENOMAI
source "arch/$SRCARCH/xenomai/Kconfig"
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

config XENO_VERSION_STRING
       string
       default @VERSION_STRING@

config XENO_VERSION_NAME
       string
       default "@VERSION_NAME@"

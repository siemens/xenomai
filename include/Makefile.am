nodist_include_HEADERS=$(CONFIG_HEADER)

include_HEADERS = version.h

SUBDIRS = 		\
	nocore		\
	boilerplate	\
	copperplate	\
	alchemy		\
	psos 		\
	vxworks

if XENO_COBALT
SUBDIRS += 		\
	cobalt 		\
	rtdm
else
SUBDIRS += mercury
endif

EXTRA_DIST = testing

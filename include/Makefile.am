nodist_include_HEADERS=$(CONFIG_HEADER)

include_HEADERS = version.h

SUBDIRS = 		\
	nocore		\
	copperplate	\
	alchemy		\
	psos 		\
	vxworks

if XENO_COBALT
SUBDIRS += 		\
	analogy 	\
	cobalt 		\
	rtdm
else
SUBDIRS += mercury
endif

EXTRA_DIST = testing

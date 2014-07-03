nodist_include_HEADERS=$(CONFIG_HEADER)

include_HEADERS = version.h

SUBDIRS = 		\
	nocore		\
	boilerplate	\
	copperplate	\
	smokey		\
	alchemy		\
	psos 		\
	vxworks

if XENO_COBALT
SUBDIRS += 		\
	cobalt 		\
	rtdm		\
	trank
else
SUBDIRS += mercury
endif

DIST_SUBDIRS = 		\
	alchemy		\
	boilerplate	\
	cobalt 		\
	copperplate	\
	mercury		\
	nocore		\
	psos 		\
	rtdm		\
	smokey		\
	trank		\
	vxworks

#include <xeno_config.h>
#include "git-stamp.h"

#ifndef GIT_STAMP
#define GIT_STAMP  ""
#endif

#ifdef CONFIG_XENO_COBALT
#define core_suffix "/cobalt v"
#else /* CONFIG_XENO_MERCURY */
#define core_suffix "/mercury v"
#endif

const char *xenomai_version_string = PACKAGE_NAME \
	core_suffix PACKAGE_VERSION " -- " GIT_STAMP;

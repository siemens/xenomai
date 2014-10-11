#ifndef __ANALOGY_CALIBRATION_H__
#define __ANALOGY_CALIBRATION_H__

/*
 * internal definitions between the xenomai utils and the library.
 * no need to expose them to the USER
 *
 */
#define ELEMENT_FIELD_FMT	"%s_%d:%s"
#define ELEMENT_FMT		"%s:%s"
#define COEFF_FMT		ELEMENT_FIELD_FMT"_%d"

#define PLATFORM_STR		"platform"
#define CALIBRATION_SUBD_STR	"calibration"
#define MEMORY_SUBD_STR		"memory"
#define AI_SUBD_STR		"analog_input"
#define AO_SUBD_STR		"analog_output"

#define INDEX_STR	"index"
#define ELEMENTS_STR	"elements"
#define CHANNEL_STR	"channel"
#define RANGE_STR	"range"
#define EXPANSION_STR	"expansion_origin"
#define NBCOEFF_STR	"nbcoeff"
#define COEFF_STR	"coeff"
#define BOARD_STR	"board_name"
#define DRIVER_STR	"driver_name"

struct polynomial {
	double expansion_origin;
	double *coefficients;
	int nb_coefficients;
	int order;
};

struct subdevice_calibration_node {
	struct holder node;
	struct polynomial *polynomial;
	unsigned channel;
	unsigned range;
};


#endif

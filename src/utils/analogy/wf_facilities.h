#ifndef __SIGNAL_GENERATION_H__
#define  __SIGNAL_GENERATION_H__

#include <stdio.h>

#define MAX_SAMPLE_COUNT 8096
#define MIN_SAMPLE_COUNT 2

#define WAVEFORM_SINE 0
#define WAVEFORM_SAWTOOTH 1
#define WAVEFORM_TRIANGULAR 2
#define WAVEFORM_STEPS 3

struct waveform_config {
	
	/* Waveform stuff */
	int wf_kind;
	double wf_frequency;
	double wf_amplitude;
	double wf_offset;

	/* Sampling stuff */
	double spl_frequency;
	int spl_count;
};

void a4l_wf_init_sine(struct waveform_config *config, double *values);
void a4l_wf_init_sawtooth(struct waveform_config *config, double *values);
void a4l_wf_init_triangular(struct waveform_config *config, double *values);
void a4l_wf_init_steps(struct waveform_config *config, double *values);
void a4l_wf_set_sample_count(struct waveform_config *config);
int a4l_wf_check_config(struct waveform_config *config);
void a4l_wf_init_values(struct waveform_config *config, double *values);
void a4l_wf_dump_values(struct waveform_config *config, double *values);

#endif /*  __SIGNAL_GENERATION_H__ */

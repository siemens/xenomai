#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <math.h>

#include "wf_facilities.h"

#ifndef PI
#define PI 3.14159265358979323846
#endif

void a4l_wf_init_sine(struct waveform_config *config, double *values)
{
	int i;

	double ratio = config->wf_frequency / config->spl_frequency;

	for (i = 0; i < config->spl_count; i++) {

		values[i] = config->wf_offset -
			config->wf_amplitude / 2 + 
			0.5 * config->wf_amplitude * cos(i * 2 * PI * ratio);
	}
}

void a4l_wf_init_sawtooth(struct waveform_config *config, double *values)
{
	int i;

	double ratio = config->wf_frequency / config->spl_frequency;

	for (i = 0; i < config->spl_count; i++) {
		
		int period_idx = (int)floor(i * ratio);

		values[i] = config->wf_offset -
			config->wf_amplitude / 2 -
			period_idx * config->wf_amplitude +
			i * ratio * config->wf_amplitude;
	}
}

void a4l_wf_init_triangular(struct waveform_config *config, double *values)
{
	int i;

	double ratio = config->wf_frequency / config->spl_frequency;

	for (i = 0; i < config->spl_count; i++) {

		int period_idx = (int)floor(i * ratio);
		int half_period_idx = (int)floor(i * 2 * ratio);
		int rise = ((half_period_idx % 2) == 0) ? 1 : 0;

		if (rise) {
			values[i] = config->wf_offset - 
				config->wf_amplitude / 2 -
				2 * period_idx * config->wf_amplitude +
				2 * i * ratio * config->wf_amplitude;
		} else {
			values[i] = config->wf_offset -
				config->wf_amplitude / 2 +
				2 * (period_idx + 1) * config->wf_amplitude - 
				2 * i * ratio * config->wf_amplitude;
		}
	}
}

void a4l_wf_init_steps(struct waveform_config *config, double *values)
{
	int i;
	
	double ratio = config->wf_frequency / config->spl_frequency;
	
	for (i = 0; i < config->spl_count; i++) {
		int half_period_idx = (int)floor(i * 2 * ratio);
		int even = (half_period_idx % 2 == 0);
		
		values[i] = config->wf_offset - 
			config->wf_amplitude / 2 + even * config->wf_amplitude;
	}
}

void a4l_wf_set_sample_count(struct waveform_config *config)
{
	int sample_count = MIN_SAMPLE_COUNT;
	int best_count = MIN_SAMPLE_COUNT;
	double lowest_diff = INFINITY;

	while (sample_count < MAX_SAMPLE_COUNT) {

		double ratio = (double)sample_count * 
			(config->wf_frequency / config->spl_frequency);
		int ceiling = ceil(ratio);
		double diff = (double)ceiling - ratio;

		assert(diff >= 0);

		if (diff < lowest_diff) {
			lowest_diff = diff;
			best_count = sample_count;
		}

		if (diff == 0)
			break;

		sample_count++;
	}

	if (lowest_diff != 0) {
		fprintf(stderr, 
			"Warning: unable to create a contiguous signal\n");
		fprintf(stderr, "Warning: an approximation is performed\n");
	}

	config->spl_count = best_count;
}

int a4l_wf_check_config(struct waveform_config *config)
{

	if (config->wf_amplitude == 0)
		fprintf(stderr, "Warning: the signal will be constant\n");

	if (config->wf_frequency * 2 > config->spl_frequency) {
		fprintf(stderr, 
			"Error: the sampling frequency is not correct\n");
		fprintf(stderr, 
			"Error: sampling frequency >= 2 * signal frequency\n");
		return -EINVAL;
	}

	/* TODO: check with find_range */
	
	return 0;
}

static void (* init_values[])(struct waveform_config *, double *) = {
	a4l_wf_init_sine,
	a4l_wf_init_sawtooth,
	a4l_wf_init_triangular,
	a4l_wf_init_steps,
};

void a4l_wf_init_values(struct waveform_config *config, double *values)
{
	init_values[config->wf_kind](config, values);
}

void a4l_wf_dump_values(struct waveform_config *config, double *values)
{
	int i;
	
	for (i = 0; i < config->spl_count; i++)
		fprintf(stderr, "%f\n", values[i]);
}


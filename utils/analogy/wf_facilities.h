/*
 * Analogy for Linux, test program for waveform generation
 *
 * Copyright (C) 1997-2000 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2008 Alexis Berlemont <alexis.berlemont@free.fr>
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
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

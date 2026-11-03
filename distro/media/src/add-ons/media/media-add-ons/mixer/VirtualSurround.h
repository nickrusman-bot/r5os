/*
 * Copyright 2026, R5OS.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Synthetic headphone downmix. One delay and one gain per ear.
 * No measured head-related impulse response is shipped.
 */


#ifndef VIRTUAL_SURROUND_H
#define VIRTUAL_SURROUND_H


#include <stdint.h>


/* Same order as the low 12 bits of enum media_multi_channels. */
#define VIRTUAL_SURROUND_SPEAKERS	12
#define VIRTUAL_SURROUND_HISTORY	128


struct virtual_surround {
	int			delay_left[VIRTUAL_SURROUND_SPEAKERS];
	int			delay_right[VIRTUAL_SURROUND_SPEAKERS];
	float		gain_left[VIRTUAL_SURROUND_SPEAKERS];
	float		gain_right[VIRTUAL_SURROUND_SPEAKERS];
	float		history[VIRTUAL_SURROUND_SPEAKERS][VIRTUAL_SURROUND_HISTORY];
	int			position;
	float		sample_rate;
	int			ready;
};


void virtual_surround_init(virtual_surround* state);
void virtual_surround_prepare(virtual_surround* state, float sample_rate);
void virtual_surround_mix(virtual_surround* state, const float* planar,
	int speaker_count, int frames, float* interleaved_stereo);


#endif

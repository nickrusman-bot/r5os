/*
 * Copyright 2026, R5OS.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Each speaker is a single impulse: the near ear is louder, the far ear
 * is later. The delay is the Woodworth formula for a head radius of
 * 8.75 cm. It is not a recording of a person.
 */


#include "VirtualSurround.h"

#include <math.h>
#include <string.h>


static const float kHeadRadius = 0.0875f;
static const float kSpeedOfSound = 343.0f;
static const float kHalfPi = 1.57079633f;


/* 0 is in front. Positive is to the listener's right. */
static const float kAzimuth[VIRTUAL_SURROUND_SPEAKERS] = {
	-0.52359879f,
	0.52359879f,
	0.0f,
	0.0f,
	-1.91986218f,
	1.91986218f,
	-0.26179939f,
	0.26179939f,
	3.14159265f,
	-1.57079633f,
	1.57079633f,
	0.0f
};


static int
symmetric_speaker(int speaker)
{
	return speaker == 2 || speaker == 3 || speaker == 8 || speaker == 11;
}


static void
place_delay(float seconds, float sample_rate, int* delay)
{
	int samples = (int)(seconds * sample_rate + 0.5f);
	if (samples < 0)
		samples = 0;
	if (samples >= VIRTUAL_SURROUND_HISTORY)
		samples = VIRTUAL_SURROUND_HISTORY - 1;
	*delay = samples;
}


void
virtual_surround_init(virtual_surround* state)
{
	memset(state, 0, sizeof(*state));
}


void
virtual_surround_prepare(virtual_surround* state, float sample_rate)
{
	if (sample_rate < 1.0f) {
		state->ready = 0;
		return;
	}
	if (state->ready && state->sample_rate == sample_rate)
		return;

	const float scale = kHeadRadius / kSpeedOfSound;
	for (int speaker = 0; speaker < VIRTUAL_SURROUND_SPEAKERS; speaker++) {
		if (symmetric_speaker(speaker)) {
			float gain = 0.707f;
			if (speaker == 3)
				gain = 0.5f;
			else if (speaker == 8)
				gain = 0.7f;
			state->delay_left[speaker] = 0;
			state->delay_right[speaker] = 0;
			state->gain_left[speaker] = gain;
			state->gain_right[speaker] = gain;
			continue;
		}

		float azimuth = kAzimuth[speaker];
		float absA = azimuth < 0.0f ? -azimuth : azimuth;
		float clamped = absA > kHalfPi ? kHalfPi : absA;
		float extra = absA > kHalfPi ? scale * (absA - kHalfPi) : 0.0f;
		float itd = scale * (clamped + sinf(clamped));
		float rear = absA > 1.74532925f ? 0.9f : 1.0f;
		float far = powf(10.0f, -(12.0f * sinf(clamped)) / 20.0f) * rear;
		int ipsi = 0;
		int contra = 0;
		place_delay(extra, sample_rate, &ipsi);
		place_delay(itd + extra, sample_rate, &contra);
		if (azimuth < 0.0f) {
			state->delay_left[speaker] = ipsi;
			state->delay_right[speaker] = contra;
			state->gain_left[speaker] = rear;
			state->gain_right[speaker] = far;
		} else {
			state->delay_left[speaker] = contra;
			state->delay_right[speaker] = ipsi;
			state->gain_left[speaker] = far;
			state->gain_right[speaker] = rear;
		}
	}

	memset(state->history, 0, sizeof(state->history));
	state->position = 0;
	state->sample_rate = sample_rate;
	state->ready = 1;
}


void
virtual_surround_mix(virtual_surround* state, const float* planar,
	int speaker_count, int frames, float* interleaved_stereo)
{
	if (state == NULL || !state->ready || frames <= 0
		|| interleaved_stereo == NULL) {
		return;
	}

	for (int frame = 0; frame < frames; frame++) {
		float left = 0.0f;
		float right = 0.0f;
		int pos = state->position;
		for (int speaker = 0; speaker < VIRTUAL_SURROUND_SPEAKERS; speaker++) {
			float sample = 0.0f;
			if (planar != NULL && speaker < speaker_count)
				sample = planar[speaker * frames + frame];
			state->history[speaker][pos] = sample;

			int leftIndex = pos - state->delay_left[speaker];
			if (leftIndex < 0)
				leftIndex += VIRTUAL_SURROUND_HISTORY;
			int rightIndex = pos - state->delay_right[speaker];
			if (rightIndex < 0)
				rightIndex += VIRTUAL_SURROUND_HISTORY;
			left += state->history[speaker][leftIndex]
				* state->gain_left[speaker];
			right += state->history[speaker][rightIndex]
				* state->gain_right[speaker];
		}
		interleaved_stereo[frame * 2] = left;
		interleaved_stereo[frame * 2 + 1] = right;
		state->position++;
		if (state->position >= VIRTUAL_SURROUND_HISTORY)
			state->position = 0;
	}
}

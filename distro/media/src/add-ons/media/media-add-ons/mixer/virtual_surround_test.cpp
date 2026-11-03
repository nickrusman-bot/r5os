/*
 * Copyright 2026, R5OS.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Host check for the synthetic downmix. Not installed in the image.
 */


#include "VirtualSurround.h"

#include <stdio.h>
#include <string.h>


static int gFailures = 0;


static void
expect(int condition, const char* name)
{
	if (condition) {
		printf("ok %s\n", name);
		return;
	}
	printf("FAIL %s\n", name);
	gFailures++;
}


static int
peak_index(const float* interleaved, int frames, int channel)
{
	int best = 0;
	float peak = -1.0f;
	for (int i = 0; i < frames; i++) {
		float value = interleaved[i * 2 + channel];
		if (value < 0.0f)
			value = -value;
		if (value > peak) {
			peak = value;
			best = i;
		}
	}
	return best;
}


int
main()
{
	virtual_surround state;
	virtual_surround_init(&state);
	virtual_surround_prepare(&state, 48000.0f);

	expect(state.ready, "prepared");
	expect(state.delay_left[0] == 0, "left speaker near ear has no delay");
	expect(state.delay_right[0] > state.delay_left[0],
		"left speaker far ear is later");
	expect(state.gain_left[0] > 0.95f && state.gain_left[0] < 1.05f,
		"left speaker near gain");
	expect(state.gain_right[0] < state.gain_left[0],
		"left speaker far ear is quieter");
	expect(state.gain_left[2] > 0.7f && state.gain_left[2] < 0.72f
		&& state.gain_left[2] == state.gain_right[2],
		"center is equal in both ears");
	expect(state.delay_left[4] > 0, "rear left near ear is delayed");
	expect(state.delay_right[4] > state.delay_left[4],
		"rear left far ear is later than the near ear");
	expect(state.delay_right[0] < VIRTUAL_SURROUND_HISTORY,
		"48 kHz delay fits the history");

	virtual_surround high;
	virtual_surround_init(&high);
	virtual_surround_prepare(&high, 96000.0f);
	expect(high.delay_right[0] > state.delay_right[0],
		"96 kHz uses more samples for the same delay");
	expect(high.delay_right[0] < VIRTUAL_SURROUND_HISTORY,
		"96 kHz delay fits the history");

	const int frames = 80;
	float speakers[VIRTUAL_SURROUND_SPEAKERS * 80];
	float out[160];
	memset(speakers, 0, sizeof(speakers));
	speakers[0] = 1.0f;
	virtual_surround_mix(&state, speakers, VIRTUAL_SURROUND_SPEAKERS, frames,
		out);

	expect(out[0] > 0.95f && out[0] < 1.05f, "left impulse in the left ear");
	expect(out[1] < 0.05f, "left impulse is not already in the right ear");
	int rightPeak = peak_index(out, frames, 1);
	expect(rightPeak == state.delay_right[0], "right ear peaks at the delay");
	expect(out[rightPeak * 2 + 1] > 0.4f && out[rightPeak * 2 + 1] < 0.7f,
		"far-ear level follows the angle");

	float splitA[2];
	float splitB[158];
	virtual_surround split;
	virtual_surround_init(&split);
	virtual_surround_prepare(&split, 48000.0f);
	virtual_surround_mix(&split, speakers, VIRTUAL_SURROUND_SPEAKERS, 1,
		splitA);
	memset(speakers, 0, sizeof(speakers));
	virtual_surround_mix(&split, speakers, VIRTUAL_SURROUND_SPEAKERS,
		frames - 1, splitB);
	expect(splitA[0] == out[0] && splitA[1] == out[1],
		"first frame matches a split buffer");
	expect(splitB[(state.delay_right[0] - 1) * 2 + 1]
		== out[state.delay_right[0] * 2 + 1],
		"delay continues into the next buffer");

	float silent[VIRTUAL_SURROUND_SPEAKERS * VIRTUAL_SURROUND_HISTORY];
	float tail[VIRTUAL_SURROUND_HISTORY * 2];
	memset(silent, 0, sizeof(silent));
	virtual_surround_mix(&state, silent, VIRTUAL_SURROUND_SPEAKERS,
		VIRTUAL_SURROUND_HISTORY, tail);
	float leftover = 0.0f;
	for (int i = 0; i < VIRTUAL_SURROUND_HISTORY * 2; i++) {
		float value = tail[i] < 0.0f ? -tail[i] : tail[i];
		if (value > leftover)
			leftover = value;
	}
	expect(leftover == 0.0f, "history clears after one delay line");

	if (gFailures != 0) {
		printf("virtual_surround_test: %d failed\n", gFailures);
		return 1;
	}
	printf("virtual_surround_test: all checks passed\n");
	return 0;
}

/*
 * Copyright 2026, R5OS.
 * SPDX-License-Identifier: MIT
 *
 * MIDI 2.0 channel voice to MIDI 1.0 bytes. Appendix D, Translation
 * (D.2). Field positions are section 7.4.
 */


#ifndef _MIDI2_TRANSLATE_H
#define _MIDI2_TRANSLATE_H

#include <stddef.h>
#include <stdint.h>


/* out holds 12 bytes. Returns the MIDI 1.0 length, or 0 when Appendix D,
 * Messages That Cannot Be Translated to MIDI 1.0 (D.2.8), applies.
 */
inline constexpr int
midi2_channel_voice_to_midi1(const uint32_t* words, size_t wordCount,
	uint8_t* out)
{
	if (words == NULL || out == NULL || wordCount != 2
		|| (words[0] >> 28) != 0x4)
		return 0;

	uint8_t status = (words[0] >> 20) & 0x0f;
	uint8_t channel = (words[0] >> 16) & 0x0f;
	uint8_t statusByte = (uint8_t)((status << 4) | channel);
	uint8_t index = (uint8_t)((words[0] >> 8) & 0x7f);

	if (status == 0x8 || status == 0x9) {
		uint16_t velocity = (uint16_t)(words[1] >> 16);
		uint8_t seven = (uint8_t)(velocity >> 9);
		if (status == 0x9 && seven == 0)
			seven = 1;
		out[0] = statusByte;
		out[1] = index;
		out[2] = seven;
		return 3;
	}
	if (status == 0xa || status == 0xb) {
		out[0] = statusByte;
		out[1] = index;
		out[2] = (uint8_t)(words[1] >> 25);
		return 3;
	}
	if (status == 0xd) {
		out[0] = statusByte;
		out[1] = (uint8_t)(words[1] >> 25);
		return 2;
	}
	if (status == 0xe) {
		uint16_t bend = (uint16_t)(words[1] >> 18);
		out[0] = statusByte;
		out[1] = (uint8_t)(bend & 0x7f);
		out[2] = (uint8_t)((bend >> 7) & 0x7f);
		return 3;
	}
	if (status == 0xc) {
		uint8_t program = (uint8_t)((words[1] >> 24) & 0x7f);
		int length = 0;
		if ((words[0] & 0x1) != 0) {
			out[0] = (uint8_t)(0xb0 | channel);
			out[1] = 0;
			out[2] = (uint8_t)((words[1] >> 8) & 0x7f);
			out[3] = (uint8_t)(0xb0 | channel);
			out[4] = 32;
			out[5] = (uint8_t)(words[1] & 0x7f);
			length = 6;
		}
		out[length] = statusByte;
		out[length + 1] = program;
		return length + 2;
	}
	if (status == 0x2 || status == 0x3) {
		uint8_t msbController = status == 0x2 ? 101 : 99;
		uint8_t lsbController = status == 0x2 ? 100 : 98;
		uint8_t dataMsb = (uint8_t)(words[1] >> 25);
		uint8_t dataLsb = (uint8_t)((words[1] >> 18) & 0x7f);
		uint8_t cc = (uint8_t)(0xb0 | channel);
		uint8_t pair[4] = { msbController, (uint8_t)((words[0] >> 8) & 0x7f),
			lsbController, (uint8_t)(words[0] & 0x7f) };
		for (int i = 0; i < 2; i++) {
			out[i * 3] = cc;
			out[i * 3 + 1] = pair[i * 2];
			out[i * 3 + 2] = pair[i * 2 + 1];
		}
		out[6] = cc;
		out[7] = 6;
		out[8] = dataMsb;
		out[9] = cc;
		out[10] = 38;
		out[11] = dataLsb;
		return 12;
	}
	return 0;
}


#endif

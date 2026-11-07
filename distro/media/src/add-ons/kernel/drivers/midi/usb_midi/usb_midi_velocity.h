/*
 * Copyright 2026, R5OS.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Kernel copies of the note translation in Midi2Defs.h. A kernel driver does
 * not include that header. Keep each function the same as the one named in
 * its comment.
 */


#ifndef USB_MIDI_VELOCITY_H
#define USB_MIDI_VELOCITY_H

#include <stdint.h>


/* Must match midi2_note_on_midi1_velocity. */
static inline constexpr uint8_t
usb_midi_note_on_midi1_velocity(uint16_t velocity)
{
	uint8_t scaled = (uint8_t)(velocity >> 9);
	return scaled == 0 ? 1 : scaled;
}


/* Must match midi2_scale_up_7_to_16. */
static inline constexpr uint16_t
usb_midi_scale_up_7_to_16(uint8_t value7)
{
	uint16_t bitShifted = (uint16_t)value7 << 9;
	if (value7 <= 64)
		return bitShifted;
	uint16_t repeat = (uint16_t)value7 & 0x3f;
	return bitShifted | (uint16_t)(repeat << 3) | (uint16_t)(repeat >> 3);
}


/* Appendix D.1.3, 7 bits to 32. Value 64 is the center, 0x80000000. */
static inline constexpr uint32_t
usb_midi_scale_up_7_to_32(uint8_t value7)
{
	uint32_t bitShifted = (uint32_t)value7 << 25;
	if (value7 <= 64)
		return bitShifted;
	uint32_t repeat = (uint32_t)value7 & 0x3fu;
	return bitShifted | (repeat << 19) | (repeat << 13) | (repeat << 7)
		| (repeat << 1) | (repeat >> 5);
}


/* Appendix D.1.3, 14 bits to 32. Center 8192 is 0x80000000. */
static inline constexpr uint32_t
usb_midi_scale_up_14_to_32(uint16_t value14)
{
	uint32_t bitShifted = (uint32_t)value14 << 18;
	if (value14 <= 8192)
		return bitShifted;
	uint32_t repeat = (uint32_t)value14 & 0x1fffu;
	return bitShifted | (repeat << 5) | (repeat >> 8);
}


struct usb_midi_translate_state {
	uint8_t		bank_msb[16];
	uint8_t		bank_lsb[16];
	uint8_t		bank_mask[16];
	uint8_t		addr_msb[16];
	uint8_t		addr_lsb[16];
	uint8_t		data_msb[16];
	uint8_t		data_lsb[16];
	uint8_t		kind[16];
	uint8_t		phase[16];
};


/* Must match midi2_translate_midi1_note. */
static inline constexpr void
usb_midi_translate_midi1_note(uint8_t channel, uint8_t note, uint8_t velocity,
	int noteOn, uint32_t words[2])
{
	uint8_t status = 0x8;
	uint16_t wide = 0x8000;
	if (!(noteOn && velocity == 0)) {
		status = noteOn ? 0x9 : 0x8;
		wide = usb_midi_scale_up_7_to_16(velocity);
	}
	words[0] = (0x4u << 28)
		| ((uint32_t)status << 20)
		| ((uint32_t)(channel & 0x0f) << 16)
		| ((uint32_t)(note & 0x7f) << 8);
	words[1] = (uint32_t)wide << 16;
}


/* One protocol per endpoint. 0x02 is MIDI 2.0 (every channel voice becomes
 * type 0x4, M2-104-UM D.3). Anything else, including an unknown protocol,
 * stays MIDI 1.0: the caller emits type 0x2, and velocity 0 stays a
 * note-on. Return 2 when words are filled, -1 when the byte is held for a
 * later bank or RPN, and 0 when the caller should emit type 0x2. */
static inline int
usb_midi_translate_channel(uint8_t protocol, uint8_t statusByte, uint8_t data1,
	uint8_t data2, struct usb_midi_translate_state* state, uint32_t words[2])
{
	uint8_t channel;
	uint8_t kind;
	if (protocol != 0x02 || words == 0)
		return 0;
	kind = statusByte & 0xf0;
	channel = statusByte & 0x0f;
	if (kind == 0x80 || kind == 0x90) {
		usb_midi_translate_midi1_note(channel, data1 & 0x7f, data2 & 0x7f,
			kind == 0x90, words);
		return 2;
	}
	if (kind == 0xa0 || kind == 0xd0) {
		uint8_t status = kind == 0xa0 ? 0x0a : 0x0d;
		words[0] = (0x4u << 28) | ((uint32_t)status << 20)
			| ((uint32_t)channel << 16);
		if (kind == 0xa0)
			words[0] |= (uint32_t)(data1 & 0x7f) << 8;
		words[1] = usb_midi_scale_up_7_to_32(kind == 0xa0
			? (data2 & 0x7f) : (data1 & 0x7f));
		return 2;
	}
	if (kind == 0xe0) {
		uint16_t bend = (uint16_t)((data2 & 0x7f) << 7) | (data1 & 0x7f);
		words[0] = (0x4u << 28) | (0x0eu << 20) | ((uint32_t)channel << 16);
		words[1] = usb_midi_scale_up_14_to_32(bend);
		return 2;
	}
	if (kind == 0xc0) {
		uint8_t bankValid = 0;
		uint8_t msb = 0;
		uint8_t lsb = 0;
		if (state != 0 && (state->bank_mask[channel] & 0x03) == 0x03) {
			bankValid = 1;
			msb = state->bank_msb[channel];
			lsb = state->bank_lsb[channel];
		}
		words[0] = (0x4u << 28) | (0x0cu << 20) | ((uint32_t)channel << 16)
			| bankValid;
		words[1] = ((uint32_t)(data1 & 0x7f) << 24) | ((uint32_t)msb << 8) | lsb;
		return 2;
	}
	if (kind != 0xb0)
		return 0;
	if (state == 0) {
		words[0] = (0x4u << 28) | (0x0bu << 20) | ((uint32_t)channel << 16)
			| ((uint32_t)(data1 & 0x7f) << 8);
		words[1] = usb_midi_scale_up_7_to_32(data2 & 0x7f);
		return 2;
	}
	data1 &= 0x7f;
	data2 &= 0x7f;
	if (data1 == 0) {
		state->bank_msb[channel] = data2;
		state->bank_mask[channel] |= 0x01;
		return -1;
	}
	if (data1 == 32) {
		state->bank_lsb[channel] = data2;
		state->bank_mask[channel] |= 0x02;
		return -1;
	}
	if (data1 == 101 || data1 == 100 || data1 == 99 || data1 == 98) {
		if (state->phase[channel] == 2) {
			/* A new address ends the previous RPN. It was already sent. */
			state->phase[channel] = 0;
		}
		if (data1 == 101 || data1 == 99)
			state->addr_msb[channel] = data2;
		else
			state->addr_lsb[channel] = data2;
		state->kind[channel] = (data1 == 101 || data1 == 100) ? 1 : 2;
		if (data1 == 100 || data1 == 98)
			state->phase[channel] = 1;
		return -1;
	}
	if (data1 == 6 || data1 == 38) {
		uint8_t status;
		if (data1 == 6) {
			state->data_msb[channel] = data2;
			/* The first data MSB is stored. A later CC 6, or CC 38, sends. */
			if (state->phase[channel] == 1) {
				state->phase[channel] = 3;
				return -1;
			}
			if (state->phase[channel] != 3 && state->phase[channel] != 2)
				return -1;
			state->phase[channel] = 2;
		} else {
			state->data_lsb[channel] = data2;
			if (state->phase[channel] != 1 && state->phase[channel] != 3
				&& state->phase[channel] != 2)
				return -1;
			state->phase[channel] = 2;
		}
		if (state->addr_msb[channel] == 0x7f && state->addr_lsb[channel] == 0x7f)
			return -1;
		status = state->kind[channel] == 2 ? 0x03 : 0x02;
		words[0] = (0x4u << 28) | ((uint32_t)status << 20)
			| ((uint32_t)channel << 16)
			| ((uint32_t)state->addr_msb[channel] << 8)
			| state->addr_lsb[channel];
		words[1] = usb_midi_scale_up_14_to_32(
			(uint16_t)((state->data_msb[channel] << 7) | state->data_lsb[channel]));
		return 2;
	}
	words[0] = (0x4u << 28) | (0x0bu << 20) | ((uint32_t)channel << 16)
		| ((uint32_t)data1 << 8);
	words[1] = usb_midi_scale_up_7_to_32(data2);
	return 2;
}


#endif
